#include "pattern_collection_generator_filtered_systematic.h"

#include "pattern_collection_generator_systematic.h"
#include "pattern_evaluator.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../algorithms/array_pool.h"
#include "../algorithms/priority_queues.h"
#include "../cost_saturation/projection.h"
#include "../cost_saturation/utils.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/math.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
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

static int get_pdb_size(const vector<int> &domain_sizes, const Pattern &pattern) {
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

bool contains_positive_finite_value(const vector<int> &values) {
    return any_of(values.begin(), values.end(),
                  [](int v) {return v > 0 && v != numeric_limits<int>::max();});
}

static bool only_free_operators_affect_pdb(
    const Pattern &pattern,
    const vector<int> &costs,
    const vector<vector<int>> &relevant_operators_per_variable) {
    for (int var : pattern) {
        for (int op : relevant_operators_per_variable[var]) {
            if (costs[op] > 0 && costs[op] != numeric_limits<int>::max()) {
                return false;
            }
        }
    }
    return true;
}

static PatternCollection get_patterns(
    const shared_ptr<AbstractTask> &task,
    int pattern_size,
    PatternOrder order,
    const vector<int> &domains,
    const utils::CountdownTimer &timer) {
    utils::Log() << "Generate patterns for size " << pattern_size << endl;
    options::Options opts;
    opts.set<int>("pattern_max_size", pattern_size);
    opts.set<bool>("only_interesting_patterns", true);
    PatternCollectionGeneratorSystematic generator(opts);
    PatternCollection patterns;
    generator.generate(
        task, [pattern_size, &patterns, &timer](const Pattern &pattern) {
            if (static_cast<int>(pattern.size()) == pattern_size) {
                patterns.push_back(pattern);
            }
            return timer.is_expired();
        }, timer);
    if (order == PatternOrder::RANDOM) {
        random_shuffle(patterns.begin(), patterns.end());
    } else if (order == PatternOrder::REVERSE) {
        reverse(patterns.begin(), patterns.end());
    } else if (order == PatternOrder::INCREASING_PDB_SIZE) {
        sort(patterns.begin(), patterns.end(),
             [&domains](const Pattern &p1, const Pattern &p2) {
                 return get_pdb_size(domains, p1) < get_pdb_size(domains, p2);
             });
    } else if (order == PatternOrder::DECREASING_PDB_SIZE) {
        sort(patterns.begin(), patterns.end(),
             [&domains](const Pattern &p1, const Pattern &p2) {
                 return get_pdb_size(domains, p1) > get_pdb_size(domains, p2);
             });
    } else {
        assert(order == PatternOrder::ORIGINAL);
    }
    return patterns;
}


class SequentialPatternGenerator {
    shared_ptr<AbstractTask> task;
    int max_pattern_size;
    PatternOrder order;
    vector<int> domains;
    array_pool::ArrayPool<int> patterns;
    int cached_pattern_size;
public:
    SequentialPatternGenerator(
        const shared_ptr<AbstractTask> &task, int max_pattern_size_, PatternOrder order)
        : task(task),
          max_pattern_size(max_pattern_size_),
          order(order),
          domains(get_variable_domains(TaskProxy(*task))),
          cached_pattern_size(0) {
        assert(max_pattern_size_ >= 0);
        max_pattern_size = min(
            max_pattern_size, static_cast<int>(TaskProxy(*task).get_variables().size()));
    }

    Pattern get_pattern(int pattern_id, const utils::CountdownTimer &timer) {
        assert(pattern_id >= 0);
        if (pattern_id < patterns.size()) {
            array_pool::ArrayPoolSlice<int> slice = patterns.get_slice(pattern_id);
            return {
                       slice.begin(), slice.end()
            };
        } else if (cached_pattern_size < max_pattern_size) {
            ++cached_pattern_size;
            for (Pattern &pattern : get_patterns(
                     task, cached_pattern_size, order, domains, timer)) {
                patterns.append(move(pattern));
            }
            return get_pattern(pattern_id, timer);
        } else {
            return {};
        }
    }
};


PatternCollectionGeneratorFilteredSystematic::PatternCollectionGeneratorFilteredSystematic(
    const Options &opts)
    : max_pattern_size(opts.get<int>("max_pattern_size")),
      max_pdb_size(opts.get<int>("max_pdb_size")),
      max_collection_size(opts.get<int>("max_collection_size")),
      max_patterns(opts.get<int>("max_patterns")),
      max_time(opts.get<double>("max_time")),
      max_time_per_restart(opts.get<double>("max_time_per_restart")),
      saturate(opts.get<bool>("saturate")),
      ignore_useless_patterns(opts.get<bool>("ignore_useless_patterns")),
      store_orders(opts.get<bool>("store_orders")),
      dead_end_treatment(static_cast<DeadEndTreatment>(opts.get_enum("dead_ends"))),
      pattern_order(static_cast<PatternOrder>(opts.get_enum("order"))),
      debug(opts.get<bool>("debug")) {
}

bool PatternCollectionGeneratorFilteredSystematic::select_systematic_patterns(
    const shared_ptr<AbstractTask> &task,
    const shared_ptr<cost_saturation::TaskInfo> &task_info,
    const TaskInfo &evaluator_task_info,
    SequentialPatternGenerator &pattern_generator,
    PartialStateCollection &dead_ends,
    priority_queues::AdaptiveQueue<size_t> &pq,
    const shared_ptr<ProjectionCollection> &projections,
    PatternSet &pattern_set,
    int64_t &collection_size,
    double overall_remaining_time) {
    utils::Log log;
    utils::CountdownTimer timer(min(overall_remaining_time, max_time_per_restart));
    TaskProxy task_proxy(*task);
    State initial_state = task_proxy.get_initial_state();
    vector<int> variable_domains = get_variable_domains(task_proxy);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    int pattern_id = -1;
    while (true) {
        if (timer.is_expired()) {
            log << "Reached restart time limit." << endl;
            return false;
        }

        ++pattern_id;

        pattern_computation_timer->resume();
        Pattern pattern = pattern_generator.get_pattern(pattern_id, timer);
        pattern_computation_timer->stop();
        ++num_evaluated_patterns;

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

        if (ignore_useless_patterns &&
            only_free_operators_affect_pdb(pattern, costs, relevant_operators_per_variable)) {
            if (debug)
                log << "Only free operators affect " << pattern << endl;
            continue;
        }

        projection_computation_timer->resume();
        PatternEvaluator pattern_evaluator(task_proxy, evaluator_task_info, pattern, costs);
        projection_computation_timer->stop();

        bool select_pattern = true;
        if (saturate) {
            projection_evaluation_timer->resume();
            select_pattern = pattern_evaluator.is_useful(
                pattern, pq, dead_ends, dead_end_treatment, costs);
            projection_evaluation_timer->stop();
#ifndef NDEBUG
            vector<int> goal_distances = cost_saturation::Projection(
                task_proxy, task_info, pattern).compute_goal_distances(costs);
            if (dead_end_treatment == DeadEndTreatment::IGNORE) {
                assert(select_pattern ==
                       contains_positive_finite_value(goal_distances));
            } else if (dead_end_treatment == DeadEndTreatment::ALL) {
                assert(select_pattern ==
                       any_of(goal_distances.begin(), goal_distances.end(),
                              [](int d) {return d > 0;}));
            } else {
                assert(dead_end_treatment == DeadEndTreatment::NEW);
            }
#endif
        }

        if (select_pattern) {
            log << "Add pattern " << pattern << endl;
            unique_ptr<cost_saturation::Projection> projection =
                utils::make_unique_ptr<cost_saturation::Projection>(
                    task_proxy, task_info, pattern);
            if (saturate) {
                vector<int> goal_distances = projection->compute_goal_distances(costs);
                vector<int> saturated_costs = projection->compute_saturated_costs(
                    goal_distances, costs.size());
                cost_saturation::reduce_costs(costs, saturated_costs);
            }
            projections->push_back(move(projection));
            pattern_set.insert(pattern);
            collection_size += pdb_size;
        }
    }
}

PatternCollectionInformation PatternCollectionGeneratorFilteredSystematic::generate(
    const shared_ptr<AbstractTask> &task) {
    utils::CountdownTimer timer(max_time);
    pattern_computation_timer = utils::make_unique_ptr<utils::Timer>();
    pattern_computation_timer->stop();
    projection_computation_timer = utils::make_unique_ptr<utils::Timer>();
    projection_computation_timer->stop();
    projection_evaluation_timer = utils::make_unique_ptr<utils::Timer>();
    projection_evaluation_timer->stop();
    utils::Log log;
    TaskProxy task_proxy(*task);
    shared_ptr<cost_saturation::TaskInfo> task_info =
        make_shared<cost_saturation::TaskInfo>(task_proxy);
    TaskInfo evaluator_task_info(task_proxy);
    if (ignore_useless_patterns) {
        relevant_operators_per_variable = get_relevant_operators_per_variable(task_proxy);
    }
    SequentialPatternGenerator pattern_generator(task, max_pattern_size, pattern_order);
    priority_queues::AdaptiveQueue<size_t> pq;
    PartialStateCollection dead_ends;
    shared_ptr<ProjectionCollection> projections = make_shared<ProjectionCollection>();
    PatternSet pattern_set;
    int64_t collection_size = 0;
    num_evaluated_patterns = 0;
    bool limit_reached = false;
    while (!limit_reached) {
        int num_patterns_before = projections->size();
        limit_reached = select_systematic_patterns(
            task, task_info, evaluator_task_info, pattern_generator, dead_ends,
            pq, projections, pattern_set, collection_size,
            timer.get_remaining_time());
        int num_patterns_after = projections->size();
        log << "Patterns: " << num_patterns_after << ", collection size: "
            << collection_size << endl;
        if (store_orders && num_patterns_after > num_patterns_before) {
            cost_saturation::Order order;
            for (int i = num_patterns_before; i < num_patterns_after; ++i) {
                order.push_back(i);
            }
            cout << "Store order " << order << endl;
            cost_saturation::systematic_generator_orders_hacked.push_back(order);
        }
        if (num_patterns_after == num_patterns_before) {
            log << "Restart did not add any pattern." << endl;
            break;
        }
        if (timer.is_expired()) {
            log << "Reached overall time limit." << endl;
            break;
        }
    }

    log << "Time for computing ordered systematic patterns: "
        << *pattern_computation_timer << endl;
    log << "Time for computing ordered systematic projections: "
        << *projection_computation_timer << endl;
    log << "Time for evaluating ordered systematic projections: "
        << *projection_evaluation_timer << endl;
    double percent_selected = (num_evaluated_patterns == 0) ? 0.
        : static_cast<double>(projections->size()) / num_evaluated_patterns;
    log << "Selected ordered systematic patterns: " << projections->size()
        << "/" << num_evaluated_patterns << " = " << percent_selected << endl;

    shared_ptr<PatternCollection> patterns = make_shared<PatternCollection>();
    patterns->reserve(projections->size());
    for (auto &projection : *projections) {
        patterns->push_back(projection->get_pattern());
    }
    PatternCollectionInformation pci(task_proxy, patterns);
    pci.set_projections(projections);
    return pci;
}


static void add_options(OptionParser &parser) {
    parser.add_option<int>(
        "max_pattern_size",
        "maximum number of variables per pattern",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_pdb_size",
        "maximum number of states in a PDB",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_collection_size",
        "maximum number of states in the pattern collection",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_patterns",
        "maximum number of patterns",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for generating patterns",
        "100",
        Bounds("0.0", "infinity"));
    parser.add_option<double>(
        "max_time_per_restart",
        "maximum time in seconds for each restart",
        "10",
        Bounds("0.0", "infinity"));
    parser.add_option<bool>(
        "saturate",
        "compute saturated cost partitionings",
        "true");
    parser.add_option<bool>(
        "ignore_useless_patterns",
        "ignore patterns with only variables that are changed by free operators",
        "false");
    parser.add_option<bool>(
        "store_orders",
        "store orders",
        "true");
    vector<string> dead_end_treatments;
    dead_end_treatments.push_back("IGNORE");
    dead_end_treatments.push_back("ALL");
    dead_end_treatments.push_back("NEW");
    parser.add_enum_option(
        "dead_ends",
        dead_end_treatments,
        "how to handle dead ends",
        "NEW");
    vector<string> pattern_orders;
    pattern_orders.push_back("ORIGINAL");
    pattern_orders.push_back("RANDOM");
    pattern_orders.push_back("REVERSE");
    pattern_orders.push_back("INCREASING_PDB_SIZE");
    pattern_orders.push_back("DECREASING_PDB_SIZE");
    parser.add_enum_option(
        "order",
        pattern_orders,
        "order in which to consider patterns of the same size",
        "ORIGINAL");
    parser.add_option<bool>(
        "debug",
        "print debugging messages",
        "false");
}

static shared_ptr<PatternCollectionGenerator> _parse(OptionParser &parser) {
    add_options(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return make_shared<PatternCollectionGeneratorFilteredSystematic>(opts);
}

static Plugin<PatternCollectionGenerator> _plugin("filtered_systematic", _parse);
}
