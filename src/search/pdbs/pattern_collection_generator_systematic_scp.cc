#include "pattern_collection_generator_systematic_scp.h"

#include "pattern_evaluator.h"

#include "../plugins/plugin.h"
#include "../task_proxy.h"

#include "../algorithms/array_pool.h"
#include "../algorithms/partial_state_tree.h"
#include "../algorithms/priority_queues.h"
#include "../cost_saturation/explicit_projection_factory.h"
#include "../cost_saturation/projection.h"
#include "../cost_saturation/utils.h"
#include "../plugins/options.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/math.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>

using namespace std;

namespace pdbs {
static vector<int> get_variable_domains(const TaskProxy &task_proxy) {
    VariablesProxy variables = task_proxy.get_variables();
    vector<int> variable_domains;
    variable_domains.reserve(variables.size());
    for (VariableProxy var : variables) {
        variable_domains.push_back(var.get_domain_size());
    }
    return variable_domains;
}

static vector<vector<int>> get_relevant_operators_per_variable(
    const TaskProxy &task_proxy) {
    vector<vector<int>> operators_per_variable(task_proxy.get_variables().size());
    for (OperatorProxy op : task_proxy.get_operators()) {
        for (EffectProxy effect : op.get_effects()) {
            int var = effect.get_fact().get_variable().get_id();
            operators_per_variable[var].push_back(op.get_id());
        }
    }
    for (auto &operators : operators_per_variable) {
        operators.shrink_to_fit();
    }
    return operators_per_variable;
}

template<typename Iterable>
static int get_pdb_size(const vector<int> &domain_sizes, const Iterable &pattern) {
    int size = 1;
    for (int var : pattern) {
        if (utils::is_product_within_limit(
                size, domain_sizes[var], numeric_limits<int>::max())) {
            size *= domain_sizes[var];
        } else {
            return -1;
        }
    }
    return size;
}

template<typename Iterable>
static int get_num_active_ops(
    const Iterable &pattern,
    const TaskInfo &task_info) {
    int num_active_ops = 0;
    for (int op_id = 0; op_id < task_info.get_num_operators(); ++op_id) {
        if (task_info.operator_affects_pattern(pattern, op_id)) {
            ++num_active_ops;
        }
    }
    return num_active_ops;
}

static bool contains_positive_finite_value(const vector<int> &values) {
    return any_of(values.begin(), values.end(),
                  [](int v) {return v > 0 && v != numeric_limits<int>::max();});
}

static bool operators_with_positive_finite_costs_affect_pdb(
    const Pattern &pattern,
    const vector<int> &costs,
    const vector<vector<int>> &relevant_operators_per_variable) {
    for (int var : pattern) {
        for (int op : relevant_operators_per_variable[var]) {
            if (costs[op] > 0 && costs[op] != numeric_limits<int>::max()) {
                return true;
            }
        }
    }
    return false;
}

static unique_ptr<PatternCollection> get_patterns(
    const shared_ptr<AbstractTask> &task,
    int pattern_size,
    PatternType pattern_type,
    const utils::CountdownTimer &timer) {
    utils::g_log << "Generate patterns for size " << pattern_size << endl;
    PatternCollectionGeneratorSystematic generator(
        pattern_size, pattern_type, utils::Verbosity::NORMAL);
    unique_ptr<PatternCollection> patterns_ptr =
        utils::make_unique_ptr<PatternCollection>();
    PatternCollection &patterns = *patterns_ptr;
    generator.generate(
        task, [pattern_size, &patterns, &timer](
            const Pattern &pattern) {
            if (static_cast<int>(pattern.size()) == pattern_size) {
                patterns.push_back(pattern);
            }
            return timer.is_expired();
        }, timer);
    if (timer.is_expired()) {
        return nullptr;
    }
    return patterns_ptr;
}

static int compute_score(
    const Pattern &pattern, PatternOrder order_type, const TaskInfo &task_info) {
    if (order_type == PatternOrder::STATES_UP) {
        return get_pdb_size(task_info.domain_sizes, pattern);
    } else if (order_type == PatternOrder::STATES_DOWN) {
        return -get_pdb_size(task_info.domain_sizes, pattern);
    } else if (order_type == PatternOrder::OPS_UP) {
        return get_num_active_ops(pattern, task_info);
    } else if (order_type == PatternOrder::OPS_DOWN) {
        return -get_num_active_ops(pattern, task_info);
    } else {
        ABORT("wrong order_type");
    }
}

static void order_patterns_of_same_size(
    PatternCollection &patterns,
    PatternOrder order_type,
    const TaskInfo &task_info,
    utils::RandomNumberGenerator &rng) {
    // Use CG_DOWN for tie-breaking.
    sort(patterns.begin(), patterns.end(), greater<Pattern>());
    if (order_type == PatternOrder::CG_UP) {
        sort(patterns.begin(), patterns.end(), less<Pattern>());
    } else if (order_type == PatternOrder::CG_DOWN) {
        // This is the base order -> nothing to do.
    } else if (order_type == PatternOrder::RANDOM) {
        rng.shuffle(patterns);
    } else {
        vector<int> scores;
        scores.reserve(patterns.size());
        for (const Pattern &pattern : patterns) {
            scores.push_back(compute_score(pattern, order_type, task_info));
        }
        vector<int> order(patterns.size(), -1);
        iota(order.begin(), order.end(), 0);
        stable_sort(order.begin(), order.end(),
                    [&scores](int i, int j) {
                        return scores[i] < scores[j];
                    });
        PatternCollection sorted_patterns;
        sorted_patterns.reserve(patterns.size());
        for (int id : order) {
            sorted_patterns.push_back(move(patterns[id]));
        }
        swap(patterns, sorted_patterns);
    }
}


class SequentialPatternGenerator {
    shared_ptr<AbstractTask> task;
    const TaskInfo &task_info;
    int max_pattern_size;
    PatternType pattern_type;
    PatternOrder order_type;
    utils::RandomNumberGenerator &rng;
    vector<array_pool_template::ArrayPool<int>> patterns;
    int cached_pattern_size;
    int max_generated_pattern_size; // Only count layers that actually have patterns.
    int num_generated_patterns;
public:
    SequentialPatternGenerator(
        const shared_ptr<AbstractTask> &task,
        const TaskInfo &task_info,
        int max_pattern_size_,
        PatternType pattern_type,
        PatternOrder order,
        utils::RandomNumberGenerator &rng)
        : task(task),
          task_info(task_info),
          max_pattern_size(max_pattern_size_),
          pattern_type(pattern_type),
          order_type(order),
          rng(rng),
          cached_pattern_size(0),
          max_generated_pattern_size(0),
          num_generated_patterns(0) {
        assert(max_pattern_size_ >= 0);
        max_pattern_size = min(max_pattern_size, task_info.get_num_variables());
    }

    Pattern get_pattern(
        int pattern_id,
        const utils::CountdownTimer &timer) {
        assert(pattern_id >= 0);
        if (pattern_id < num_generated_patterns) {
            int bucket_id = -1;
            int internal_id = -1;
            int start_id = 0;
            int end_id = -1;
            for (size_t i = 0; i < patterns.size(); ++i) {
                const auto &pattern_layer = patterns[i];
                end_id += pattern_layer.size();
                if (pattern_id >= start_id && pattern_id <= end_id) {
                    internal_id = pattern_id - start_id;
                    bucket_id = i;
                    break;
                }
                start_id += pattern_layer.size();
            }
            assert(internal_id != -1);
            auto slice = patterns[bucket_id].get_slice(internal_id);
            return {
                slice.begin(), slice.end()
            };
        } else if (cached_pattern_size < max_pattern_size) {
            unique_ptr<PatternCollection> current_patterns = get_patterns(
                task, cached_pattern_size + 1, pattern_type, timer);
            if (current_patterns) {
                ++cached_pattern_size;
                if (current_patterns->empty()) {
                    utils::g_log << "Found no patterns of size "
                                 << cached_pattern_size << endl;
                } else {
                    utils::g_log << "Store " << current_patterns->size()
                                 << " patterns of size "
                                 << cached_pattern_size << endl;
                    max_generated_pattern_size = cached_pattern_size;
                    num_generated_patterns += current_patterns->size();
                    order_patterns_of_same_size(
                        *current_patterns, order_type, task_info, rng);
                    patterns.emplace_back();
                    for (Pattern &pattern : *current_patterns) {
                        patterns.back().push_back(move(pattern));
                    }
                    utils::g_log << "Finished storing patterns of size "
                                 << cached_pattern_size << endl;
                }
                return get_pattern(pattern_id, timer);
            }
        }
        return {};
    }

    int get_num_generated_patterns() const {
        return num_generated_patterns;
    }

    int get_max_generated_pattern_size() const {
        return max_generated_pattern_size;
    }
};


PatternCollectionGeneratorSystematicSCP::PatternCollectionGeneratorSystematicSCP(
    int max_pattern_size,
    int max_pdb_size,
    int max_collection_size,
    int max_patterns,
    double max_time,
    double max_time_per_restart,
    int max_evaluations_per_restart,
    int max_total_evaluations,
    bool saturate,
    bool create_complete_transition_system,
    PatternType pattern_type,
    bool ignore_useless_patterns,
    bool store_dead_ends,
    PatternOrder order,
    int random_seed,
    utils::Verbosity verbosity)
    : PatternCollectionGenerator(verbosity),
      max_pattern_size(max_pattern_size),
      max_pdb_size(max_pdb_size),
      max_collection_size(max_collection_size),
      max_patterns(max_patterns),
      max_time(max_time),
      max_time_per_restart(max_time_per_restart),
      max_evaluations_per_restart(max_evaluations_per_restart),
      max_total_evaluations(max_total_evaluations),
      saturate(saturate),
      create_complete_transition_system(create_complete_transition_system),
      pattern_type(pattern_type),
      ignore_useless_patterns(ignore_useless_patterns),
      store_dead_ends(store_dead_ends),
      pattern_order(order),
      rng(utils::get_rng(random_seed)) {
}

bool PatternCollectionGeneratorSystematicSCP::select_systematic_patterns(
    const shared_ptr<AbstractTask> &task,
    const shared_ptr<cost_saturation::TaskInfo> &task_info,
    const TaskInfo &evaluator_task_info,
    SequentialPatternGenerator &pattern_generator,
    priority_queues::AdaptiveQueue<int> &pq,
    const shared_ptr<PatternCollection> &patterns,
    const shared_ptr<ProjectionCollection> &projections,
    PatternSet &pattern_set,
    PatternSet &patterns_checked_for_dead_ends,
    int64_t &collection_size,
    double overall_remaining_time) {
    utils::CountdownTimer timer(min(overall_remaining_time, max_time_per_restart));
    int remaining_total_evaluations = max_total_evaluations - num_pattern_evaluations;
    assert(remaining_total_evaluations >= 0);
    int max_evaluations_this_restart =
        min(remaining_total_evaluations, max_evaluations_per_restart);
    int final_num_evaluations_this_restart =
        num_pattern_evaluations + max_evaluations_this_restart;
    TaskProxy task_proxy(*task);
    State initial_state = task_proxy.get_initial_state();
    vector<int> variable_domains = get_variable_domains(task_proxy);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    int pattern_id = -1;
    while (true) {
        ++pattern_id;

        pattern_computation_timer->resume();
        Pattern pattern = pattern_generator.get_pattern(pattern_id, timer);
        pattern_computation_timer->stop();

        if (timer.is_expired()) {
            log << "Reached restart time limit." << endl;
            return false;
        }

        if (num_pattern_evaluations >= final_num_evaluations_this_restart) {
            log << "Reached maximum pattern evaluations per restart." << endl;
            return false;
        }

        if (log.is_at_least_debug()) {
            log << "Pattern " << pattern_id << ": " << pattern << " size:"
                << get_pdb_size(variable_domains, pattern) << " ops:"
                << get_num_active_ops(pattern, evaluator_task_info) << endl;
        }

        if (pattern.empty()) {
            log << "Generated all patterns up to size " << max_pattern_size
                << "." << endl;
            return false;
        } else if (pattern_set.count(pattern)) {
            continue;
        }

        int pdb_size = get_pdb_size(variable_domains, pattern);
        if (pdb_size == -1 || pdb_size > max_pdb_size) {
            // Pattern is too large.
            continue;
        }

        if (static_cast<int>(projections->size()) == max_patterns) {
            log << "Reached maximum number of patterns." << endl;
            return true;
        }

        if (max_collection_size != numeric_limits<int>::max() &&
            pdb_size > static_cast<int64_t>(max_collection_size) - collection_size) {
            log << "Reached maximum collection size." << endl;
            return true;
        }

        /* If there are no state-changing transitions with positive finite costs,
           there can be no positive finite goal distances. */
        if (ignore_useless_patterns &&
            !operators_with_positive_finite_costs_affect_pdb(
                pattern, costs, relevant_operators_per_variable)) {
            if (log.is_at_least_debug()) {
                log << "Only operators with cost=0 or cost=infty affect " << pattern << endl;
            }
            continue;
        }

        bool select_pattern = true;
        if (saturate) {
            projection_evaluation_timer->resume();
            if (create_complete_transition_system) {
                unique_ptr<cost_saturation::Abstraction> projection =
                    cost_saturation::ExplicitProjectionFactory(task_proxy, pattern).convert_to_abstraction();
                // TODO: return true as soon as first settled state has positive costs.
                select_pattern = contains_positive_finite_value(projection->compute_goal_distances(costs));
            } else {
                // Only check each pattern for dead ends once.
                DeadEnds *tmp_dead_ends = dead_ends;
                if (tmp_dead_ends) {
                    if (patterns_checked_for_dead_ends.count(pattern)) {
                        tmp_dead_ends = nullptr;
                    } else {
                        patterns_checked_for_dead_ends.insert(pattern);
                    }
                }
                projection_computation_timer->resume();
                PatternEvaluator pattern_evaluator(task_proxy, evaluator_task_info, pattern, costs);
                projection_computation_timer->stop();
                select_pattern = pattern_evaluator.is_useful(pattern, pq, tmp_dead_ends, costs);

#ifndef NDEBUG
                vector<int> goal_distances = cost_saturation::Projection(
                    task_proxy, task_info, pattern).compute_goal_distances(costs);
                assert(select_pattern == contains_positive_finite_value(goal_distances));
#endif
            }
            projection_evaluation_timer->stop();
        }

        ++num_pattern_evaluations;

        if (select_pattern) {
            if (saturate) {
                log << "Add pattern " << pattern << endl;
            }
            unique_ptr<cost_saturation::Abstraction> projection;
            if (create_complete_transition_system) {
                projection = cost_saturation::ExplicitProjectionFactory(
                    task_proxy, pattern).convert_to_abstraction();
            } else {
                projection = utils::make_unique_ptr<cost_saturation::Projection>(
                    task_proxy, task_info, pattern);
            }
            if (saturate) {
                vector<int> goal_distances = projection->compute_goal_distances(costs);
                vector<int> saturated_costs = projection->compute_saturated_costs(
                    goal_distances);
                cost_saturation::reduce_costs(costs, saturated_costs);
            }
            patterns->push_back(pattern);
            projections->push_back(move(projection));
            pattern_set.insert(pattern);
            collection_size += pdb_size;
        }
    }
}

string PatternCollectionGeneratorSystematicSCP::name() const {
    return "sys-SCP pattern collection generator";
}

PatternCollectionInformation PatternCollectionGeneratorSystematicSCP::compute_patterns(
    const shared_ptr<AbstractTask> &task) {
    utils::CountdownTimer timer(max_time);
    pattern_computation_timer = utils::make_unique_ptr<utils::Timer>();
    pattern_computation_timer->stop();
    projection_computation_timer = utils::make_unique_ptr<utils::Timer>();
    projection_computation_timer->stop();
    projection_evaluation_timer = utils::make_unique_ptr<utils::Timer>();
    projection_evaluation_timer->stop();
    TaskProxy task_proxy(*task);
    task_properties::verify_no_axioms(task_proxy);
    if (!create_complete_transition_system &&
        task_properties::has_conditional_effects(task_proxy)) {
        cerr << "Error: configuration doesn't support conditional effects. "
            "Use sys_scp(..., create_complete_transition_system=true) "
            "for tasks with conditional effects."
             << endl;
        utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
    }
    shared_ptr<cost_saturation::TaskInfo> task_info =
        make_shared<cost_saturation::TaskInfo>(task_proxy);
    TaskInfo evaluator_task_info(task_proxy);
    if (ignore_useless_patterns) {
        relevant_operators_per_variable = get_relevant_operators_per_variable(task_proxy);
    }
    if (!store_dead_ends) {
        dead_ends = nullptr;
    }
    SequentialPatternGenerator pattern_generator(
        task, evaluator_task_info, max_pattern_size,
        pattern_type, pattern_order, *rng);
    priority_queues::AdaptiveQueue<int> pq;
    shared_ptr<PatternCollection> patterns = make_shared<PatternCollection>();
    shared_ptr<ProjectionCollection> projections = make_shared<ProjectionCollection>();
    PatternSet pattern_set;
    PatternSet patterns_checked_for_dead_ends;
    int64_t collection_size = 0;
    num_pattern_evaluations = 0;
    bool limit_reached = false;
    while (!limit_reached) {
        int num_patterns_before = projections->size();
        limit_reached = select_systematic_patterns(
            task, task_info, evaluator_task_info, pattern_generator,
            pq, patterns, projections, pattern_set, patterns_checked_for_dead_ends,
            collection_size, timer.get_remaining_time());
        int num_patterns_after = projections->size();
        log << "Patterns: " << num_patterns_after << ", collection size: "
            << collection_size << endl;
        if (num_patterns_after == num_patterns_before) {
            log << "Restart did not add any pattern." << endl;
            break;
        }
        if (timer.is_expired()) {
            log << "Reached overall time limit." << endl;
            break;
        }
        if (num_pattern_evaluations >= max_total_evaluations) {
            log << "Reached maximum total pattern evaluations." << endl;
            break;
        }
    }

    log << "Time for computing ordered systematic patterns: "
        << *pattern_computation_timer << endl;
    log << "Time for computing ordered systematic projections: "
        << *projection_computation_timer << endl;
    log << "Time for evaluating ordered systematic projections: "
        << *projection_evaluation_timer << endl;
    log << "Ordered systematic pattern evaluations: "
        << num_pattern_evaluations << endl;
    log << "Maximum generated ordered systematic pattern size: "
        << pattern_generator.get_max_generated_pattern_size() << endl;
    int num_generated_patterns = pattern_generator.get_num_generated_patterns();
    double percent_selected = (num_generated_patterns == 0) ? 0.
        : static_cast<double>(projections->size()) / num_generated_patterns;
    log << "Selected ordered systematic patterns: " << projections->size()
        << "/" << num_generated_patterns << " = " << percent_selected << endl;
    if (dead_ends) {
        log << "Systematic dead ends: " << dead_ends->size() << endl;
        log << "Systematic dead end tree nodes: " << dead_ends->get_num_nodes()
            << endl;
    }

    assert(patterns->size() == projections->size());
    PatternCollectionInformation pci(task_proxy, patterns, log);
    pci.set_projections(projections);
    return pci;
}


class PatternCollectionGeneratorSystematicSCPFeature
    : public plugins::TypedFeature<PatternCollectionGenerator, PatternCollectionGeneratorSystematicSCP> {
public:
    PatternCollectionGeneratorSystematicSCPFeature() : TypedFeature("sys_scp") {
        document_title("Sys-SCP patterns");
        document_synopsis(
            "Systematically generate larger (interesting) patterns but only keep "
            "a pattern if it's useful under a saturated cost partitioning. "
            "For details, see" + utils::format_conference_reference(
                {"Jendrik Seipp"},
                "Pattern Selection for Optimal Classical Planning with Saturated Cost Partitioning",
                "https://jendrikseipp.com/papers/seipp-ijcai2019.pdf",
                "Proceedings of the 28th International Joint Conference on "
                "Artificial Intelligence (IJCAI 2019)",
                "5621-5627",
                "IJCAI",
                "2019"));
        add_option<int>(
            "max_pattern_size",
            "maximum number of variables per pattern",
            "infinity",
            plugins::Bounds("1", "infinity"));
        add_option<int>(
            "max_pdb_size",
            "maximum number of states in a PDB",
            "2M",
            plugins::Bounds("1", "infinity"));
        add_option<int>(
            "max_collection_size",
            "maximum number of states in the pattern collection",
            "20M",
            plugins::Bounds("1", "infinity"));
        add_option<int>(
            "max_patterns",
            "maximum number of patterns",
            "infinity",
            plugins::Bounds("1", "infinity"));
        add_option<double>(
            "max_time",
            "maximum time in seconds for generating patterns",
            "100",
            plugins::Bounds("0.0", "infinity"));
        add_option<double>(
            "max_time_per_restart",
            "maximum time in seconds for each restart",
            "10",
            plugins::Bounds("0.0", "infinity"));
        add_option<int>(
            "max_evaluations_per_restart",
            "maximum pattern evaluations per the inner loop",
            "infinity",
            plugins::Bounds("0", "infinity"));
        add_option<int>(
            "max_total_evaluations",
            "maximum total pattern evaluations",
            "infinity",
            plugins::Bounds("0", "infinity"));
        add_option<bool>(
            "saturate",
            "only select patterns useful in saturated cost partitionings",
            "true");
        add_option<bool>(
            "create_complete_transition_system",
            "create explicit transition system (necessary for tasks with conditional effects)",
            "false");
        add_pattern_type_option(*this);
        add_option<bool>(
            "ignore_useless_patterns",
            "ignore patterns that induce no transitions with positive finite cost",
            "false");
        add_option<bool>(
            "store_dead_ends",
            "store dead ends in dead end tree (used to prune the search later)",
            "true");
        add_option<PatternOrder>(
            "order",
            "order in which to consider patterns of the same size (based on states "
            "in projection, active operators or position of the pattern variables "
            "in the partial ordering of the causal graph)",
            "cg_down");
        utils::add_rng_options_to_feature(*this);
        add_generator_options_to_feature(*this);
    }

    virtual shared_ptr<PatternCollectionGeneratorSystematicSCP>
    create_component(
        const plugins::Options &opts,
        const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<PatternCollectionGeneratorSystematicSCP>(
            opts.get<int>("max_pattern_size"),
            opts.get<int>("max_pdb_size"),
            opts.get<int>("max_collection_size"),
            opts.get<int>("max_patterns"),
            opts.get<double>("max_time"),
            opts.get<double>("max_time_per_restart"),
            opts.get<int>("max_evaluations_per_restart"),
            opts.get<int>("max_total_evaluations"),
            opts.get<bool>("saturate"),
            opts.get<bool>("create_complete_transition_system"),
            opts.get<PatternType>("pattern_type"),
            opts.get<bool>("ignore_useless_patterns"),
            opts.get<bool>("store_dead_ends"),
            opts.get<PatternOrder>("order"),
            utils::get_rng_arguments_from_options(opts),
            get_generator_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<PatternCollectionGeneratorSystematicSCPFeature> _plugin;

static plugins::TypedEnumPlugin<PatternOrder> _enum_plugin({
        {"random", "order randomly"},
        {"states_up", "order by increasing number of abstract states"},
        {"states_down", "order by decreasing number of abstract states"},
        {"ops_up", "order by increasing number of active operators"},
        {"ops_down", "order by decreasing number of active operators"},
        {"cg_up", "use lexicographical order"},
        {"cg_down", "use reverse lexicographical order"},
    });
}
