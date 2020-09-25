#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "diversifier.h"
#include "max_cost_partitioning_heuristic.h"
#include "order_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/sampling.h"
#include "../task_utils/task_properties.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/rng_options.h"
#include "../utils/timer.h"

using namespace std;

namespace cost_saturation {
static vector<vector<int>> sample_states_and_return_abstract_state_ids(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    sampling::RandomWalkSampler &sampler,
    int num_samples,
    int init_h,
    const DeadEndDetector &is_dead_end,
    double max_sampling_time) {
    assert(num_samples >= 1);
    utils::CountdownTimer sampling_timer(max_sampling_time);
    utils::Log() << "Start sampling" << endl;
    vector<vector<int>> abstract_state_ids_by_sample;
    abstract_state_ids_by_sample.push_back(
        get_abstract_state_ids(abstractions, task_proxy.get_initial_state()));
    while (static_cast<int>(abstract_state_ids_by_sample.size()) < num_samples
           && !sampling_timer.is_expired()) {
        abstract_state_ids_by_sample.push_back(
            get_abstract_state_ids(abstractions, sampler.sample_state(init_h, is_dead_end)));
    }
    utils::Log() << "Samples: " << abstract_state_ids_by_sample.size() << endl;
    utils::Log() << "Sampling time: " << sampling_timer.get_elapsed_time() << endl;
    return abstract_state_ids_by_sample;
}

// TODO: avoid code duplication
static void extract_useful_abstraction_functions(
    const vector<CostPartitioningHeuristic> &cp_heuristics,
    Abstractions &abstractions,
    AbstractionFunctions &abstraction_functions) {
    int num_abstractions = abstractions.size();

    // Collect IDs of useful abstractions.
    vector<bool> useful_abstractions(num_abstractions, false);
    for (const auto &cp_heuristic : cp_heuristics) {
        cp_heuristic.mark_useful_abstractions(useful_abstractions);
    }

    abstraction_functions.reserve(num_abstractions);
    for (int i = 0; i < num_abstractions; ++i) {
        if (useful_abstractions[i]) {
            abstraction_functions.push_back(abstractions[i]->extract_abstraction_function());
        } else {
            abstraction_functions.push_back(nullptr);
        }
    }
    assert(abstraction_functions.size() == abstractions.size());
}


SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(
    const options::Options &opts,
    Abstractions &&abstractions_,
    CPHeuristics &&cp_heuristics_)
    : Heuristic(opts),
      order_generator(opts.get<shared_ptr<OrderGenerator>>("orders")),
      saturator(opts.get<Saturator>("saturator")),
      cp_function(get_cp_function_from_options(opts)),
      abstractions(move(abstractions_)),
      cp_heuristics(move(cp_heuristics_)),
      interval(opts.get<int>("interval")),
      max_time(opts.get<double>("max_time")),
      max_size_kb(opts.get<int>("max_size")),
      use_offline_samples(opts.get<bool>("use_offline_samples")),
      num_samples(opts.get<int>("samples")),
      use_evaluated_state_as_sample(opts.get<bool>("use_evaluated_state_as_sample")),
      debug(opts.get<bool>("debug")),
      costs(task_properties::get_operator_costs(task_proxy)),
      improve_heuristic(true),
      should_compute_scp_for_bellman(false),
      size_kb(0),
      num_evaluated_states(0),
      num_scps_computed(0) {
    if (opts.get<double>("max_optimization_time") != 0.0) {
        ABORT("Order optimization is not implemented for online SCP.");
    }
    if (opts.get<int>("max_orders") != INF) {
        ABORT("Limiting the number of orders is not implemented for online SCP.");
    }
    for (auto &cp : cp_heuristics) {
        size_kb += cp.estimate_size_in_kb();
    }

    fact_id_offsets.reserve(task_proxy.get_variables().size());
    int num_facts = 0;
    for (VariableProxy var : task_proxy.get_variables()) {
        fact_id_offsets.push_back(num_facts);
        num_facts += var.get_domain_size();
    }
    cout << "Fact ID offsets: " << fact_id_offsets << endl;

    if (interval == -1) {
        seen_facts.resize(num_facts, false);
    } else if (interval == -2) {
        seen_fact_pairs.resize(num_facts);
        for (int fact_id = 0; fact_id < num_facts; ++fact_id) {
            seen_fact_pairs[fact_id].resize(num_facts, false);
        }
    }

    if (use_offline_samples) {
        setup_diversifier(*utils::parse_rng_from_options(opts));
    }

    improve_heuristic_timer = utils::make_unique_ptr<utils::Timer>(false);
}

SaturatedCostPartitioningOnlineHeuristic::~SaturatedCostPartitioningOnlineHeuristic() {
    print_statistics();
}

void SaturatedCostPartitioningOnlineHeuristic::setup_diversifier(
    utils::RandomNumberGenerator &rng) {
    State initial_state = task_proxy.get_initial_state();

    // Compute h(s_0) using a greedy order for s_0.
    vector<int> abstract_state_ids_for_init = get_abstract_state_ids(
        abstractions, initial_state);
    Order order_for_init = order_generator->compute_order_for_state(
        abstract_state_ids_for_init, true);
    CostPartitioningHeuristic cp_for_init = cp_function(
        abstractions, order_for_init, costs, abstract_state_ids_for_init);
    int init_h = cp_for_init.compute_heuristic(abstract_state_ids_for_init);

    sampling::RandomWalkSampler sampler(task_proxy, rng);
    DeadEndDetector is_dead_end =
        [this, &cp_for_init](const State &state) {
            return cp_for_init.compute_heuristic(
                get_abstract_state_ids(abstractions, state)) == INF;
        };

    double max_sampling_time = max_time / 2;
    diversifier = utils::make_unique_ptr<Diversifier>(
        sample_states_and_return_abstract_state_ids(
            task_proxy, abstractions, sampler, num_samples, init_h, is_dead_end, max_sampling_time));
}

bool SaturatedCostPartitioningOnlineHeuristic::visit_fact_pair(int fact_id1, int fact_id2) {
    if (fact_id1 > fact_id2) {
        swap(fact_id1, fact_id2);
    }
    assert(fact_id1 < fact_id2);
    bool novel = !seen_fact_pairs[fact_id1][fact_id2];
    seen_fact_pairs[fact_id1][fact_id2] = true;
    return novel;
}

bool SaturatedCostPartitioningOnlineHeuristic::is_novel(
    OperatorID op_id, const GlobalState &state) {
    if (interval == -1) {
        bool novel = false;
        for (EffectProxy effect : task_proxy.get_operators()[op_id].get_effects()) {
            FactPair fact = effect.get_fact().get_pair();
            int fact_id = get_fact_id(fact.var, fact.value);
            if (!seen_facts[fact_id]) {
                seen_facts[fact_id] = true;
                novel = true;
            }
        }
        return novel;
    } else if (interval == -2) {
        int num_vars = fact_id_offsets.size();
        bool novel = false;
        for (EffectProxy effect : task_proxy.get_operators()[op_id].get_effects()) {
            FactPair fact1 = effect.get_fact().get_pair();
            int fact_id1 = get_fact_id(fact1.var, fact1.value);
            for (int var2 = 0; var2 < num_vars; ++var2) {
                if (fact1.var == var2) {
                    continue;
                }
                FactPair fact2(var2, state[var2]);
                int fact_id2 = get_fact_id(fact2.var, fact2.value);
                if (visit_fact_pair(fact_id1, fact_id2)) {
                    novel = true;
                }
            }
        }
        return novel;
    } else {
        ABORT("invalid value for interval");
    }
}

void SaturatedCostPartitioningOnlineHeuristic::notify_initial_state(
    const GlobalState &initial_state) {
    if (interval >= 0) {
        return;
    }

    heuristic_cache[initial_state].novel = true;
    int num_vars = fact_id_offsets.size();
    if (interval == -1) {
        for (int var = 0; var < num_vars; ++var) {
            seen_facts[get_fact_id(var, initial_state[var])] = true;
        }
    } else if (interval == -2) {
        for (int var1 = 0; var1 < num_vars; ++var1) {
            int fact_id1 = get_fact_id(var1, initial_state[var1]);
            for (int var2 = var1 + 1; var2 < num_vars; ++var2) {
                int fact_id2 = get_fact_id(var2, initial_state[var2]);
                visit_fact_pair(fact_id1, fact_id2);
            }
        }
    } else {
        ABORT("invalid value for interval");
    }
}

void SaturatedCostPartitioningOnlineHeuristic::notify_state_transition(
    const GlobalState &, OperatorID op_id, const GlobalState &global_state) {
    if (!improve_heuristic) {
        return;
    }

    if (interval >= 0) {
        return;
    }

    // We only need to compute novelty for new states.
    if (heuristic_cache[global_state].h == NO_VALUE) {
        improve_heuristic_timer->resume();
        heuristic_cache[global_state].novel = is_novel(op_id, global_state);
        improve_heuristic_timer->stop();
    }
}

int SaturatedCostPartitioningOnlineHeuristic::get_fact_id(int var, int value) const {
    return fact_id_offsets[var] + value;
}

bool SaturatedCostPartitioningOnlineHeuristic::should_compute_scp(const GlobalState &global_state) {
    // This check needs to come first because the Bellman tests already fills the cache.
    if (should_compute_scp_for_bellman) {
        assert(interval == 0);
        return true;
    } else if (num_orders_used_for_state[global_state] != 0) {
        // We are reevaluating this state, so we might already have computed a SCP for it.
        return false;
    } else if (interval > 0) {
        return num_evaluated_states % interval == 0;
    } else if (interval == 0) {
        return should_compute_scp_for_bellman;
    } else if (interval == -1 || interval == -2) {
        return heuristic_cache[global_state].novel;
    } else {
        ABORT("invalid value for interval");
    }
}

static int compute_max_h_over_suffix(
    const CPHeuristics &cp_heuristics,
    const vector<int> &abstract_state_ids,
    int suffix_start) {
    int max_h = 0;
    for (size_t i = suffix_start; i < cp_heuristics.size(); ++i) {
        const CostPartitioningHeuristic &cp_heuristic = cp_heuristics[i];
        int sum_h = cp_heuristic.compute_heuristic(abstract_state_ids);
        if (sum_h == INF) {
            return INF;
        }
        max_h = max(max_h, sum_h);
    }
    assert(max_h >= 0);
    return max_h;
}

int SaturatedCostPartitioningOnlineHeuristic::compute_heuristic(
    const GlobalState &global_state) {
    if (improve_heuristic) {
        improve_heuristic_timer->resume();
    }

    State state = convert_global_state(global_state);
    vector<int> abstract_state_ids;
    if (improve_heuristic) {
        assert(!abstractions.empty() && abstraction_functions.empty());
        abstract_state_ids = get_abstract_state_ids(abstractions, state);
    } else {
        assert(abstractions.empty() && !abstraction_functions.empty());
        abstract_state_ids = get_abstract_state_ids(abstraction_functions, state);
    }

    // Retrieve cached estimate if it exists and only compute max over new orders.
    int old_h = 0;
    if (heuristic_cache[global_state].h != NO_VALUE) {
        assert(heuristic_cache[global_state].h != DEAD_END);
        old_h = heuristic_cache[global_state].h;
    }
    int new_h = compute_max_h_over_suffix(
        cp_heuristics, abstract_state_ids, num_orders_used_for_state[global_state]);
    int max_h = max(old_h, new_h);

    if (debug) {
        utils::g_log << "compute_heuristic for " << global_state.get_id() << " max_h:" << max_h << endl;
        utils::g_log << "num orders for state: " << num_orders_used_for_state[global_state] << endl;
    }
    if (max_h == INF) {
        num_orders_used_for_state[global_state] = cp_heuristics.size();
        if (improve_heuristic) {
            improve_heuristic_timer->stop();
        }
        return DEAD_END;
    }

    if (improve_heuristic &&
        ((*improve_heuristic_timer)() >= max_time || size_kb >= max_size_kb)) {
        utils::Log() << "Stop heuristic improvement phase." << endl;
        improve_heuristic = false;
        diversifier = nullptr;
        utils::release_vector_memory(fact_id_offsets);
        utils::release_vector_memory(seen_facts);
        utils::release_vector_memory(seen_fact_pairs);
        extract_useful_abstraction_functions(
            cp_heuristics, abstractions, abstraction_functions);
        utils::release_vector_memory(abstractions);
        print_diversification_statistics();
    }
    if (improve_heuristic && should_compute_scp(global_state)) {
        if (debug) {
            utils::g_log << "Compute SCP for " << global_state.get_id() << endl;
        }
        Order order = order_generator->compute_order_for_state(
            abstract_state_ids, num_evaluated_states == 0);

        CostPartitioningHeuristic cost_partitioning;
        vector<int> remaining_costs;
        if (saturator == Saturator::PERIMSTAR) {
            // Compute only the first SCP here, and the second below if h > max_h.
            remaining_costs = costs;
            cost_partitioning = compute_perim_saturated_cost_partitioning_change_costs(
                abstractions, order, remaining_costs, abstract_state_ids);
        } else {
            cost_partitioning = cp_function(abstractions, order, costs, abstract_state_ids);
        }
        ++num_scps_computed;

        int h = cost_partitioning.compute_heuristic(abstract_state_ids);

        bool is_diverse_for_state = use_evaluated_state_as_sample && (h > max_h);

        /* Adding the second SCP is only useful if the order is already diverse
           for the current state or it might be for the offline samples. */
        if (is_diverse_for_state || diversifier) {
            if (saturator == Saturator::PERIMSTAR) {
                cost_partitioning.add(
                    compute_saturated_cost_partitioning(
                        abstractions, order, remaining_costs, abstract_state_ids));
            }
        }

        bool is_diverse_for_samples = diversifier && diversifier->is_diverse(cost_partitioning);

        if (is_diverse_for_state || is_diverse_for_samples) {
            size_kb += cost_partitioning.estimate_size_in_kb();
            cp_heuristics.push_back(move(cost_partitioning));
            utils::Log() << "Stored SCPs in " << *improve_heuristic_timer << ": "
                         << cp_heuristics.size() << endl;
        }
        max_h = max(max_h, h);
    }
    num_orders_used_for_state[global_state] = cp_heuristics.size();

    if (improve_heuristic) {
        improve_heuristic_timer->stop();
    }

    ++num_evaluated_states;
    return max_h;
}

bool SaturatedCostPartitioningOnlineHeuristic::is_cached_estimate_dirty(
    const GlobalState &state) const {
    assert(is_estimate_cached(state));
    return num_orders_used_for_state[state] < static_cast<int>(cp_heuristics.size());
}

void SaturatedCostPartitioningOnlineHeuristic::compute_scp_and_store_if_diverse(
    const GlobalState &state) {
    should_compute_scp_for_bellman = true;
    compute_heuristic(state);
    should_compute_scp_for_bellman = false;
}


void SaturatedCostPartitioningOnlineHeuristic::print_diversification_statistics() const {
    // Print the number of stored lookup tables.
    int num_stored_lookup_tables = 0;
    for (const auto &cp_heuristic: cp_heuristics) {
        num_stored_lookup_tables += cp_heuristic.get_num_lookup_tables();
    }
    utils::Log() << "Stored lookup tables: " << num_stored_lookup_tables << endl;

    // Print the number of stored values.
    int num_stored_values = 0;
    for (const auto &cp_heuristic : cp_heuristics) {
        num_stored_values += cp_heuristic.get_num_heuristic_values();
    }
    utils::Log() << "Stored values: " << num_stored_values << endl;

    utils::Log() << "Time for improving heuristic: " << *improve_heuristic_timer << endl;
    utils::Log() << "Estimated heuristic size: " << size_kb << " KiB" << endl;
    utils::Log() << "Computed SCPs: " << num_scps_computed << endl;
    utils::Log() << "Stored SCPs: " << cp_heuristics.size() << endl;
}

void SaturatedCostPartitioningOnlineHeuristic::print_statistics() const {
    if (improve_heuristic) {
        print_diversification_statistics();
    }
}


static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning online heuristic",
        "");

    // TODO: document that online version is not consistent.
    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_saturator_option(parser);
    add_order_options_to_parser(parser);

    parser.add_option<int>(
        "interval",
        "select every i-th state for diversification. values -1 and -2 select "
        "states with novelty 1 and 2",
        "-2",
        Bounds("-2", "infinity"));
    parser.add_option<bool>(
        "use_offline_samples",
        "use offline samples",
        "false");
    parser.add_option<bool>(
        "use_evaluated_state_as_sample",
        "keep SCP heuristic if it improves the estimate of the evaluated state",
        "true");
    parser.add_option<bool>(
        "diversify_offline",
        "add diverse SCP heuristics found offline",
        "false");
    parser.add_option<bool>(
        "debug",
        "print debug output",
        "false");

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"));
    CPHeuristics cp_heuristics = {};
    if (opts.get<bool>("diversify_offline")) {
        cp_heuristics =
            get_cp_heuristic_collection_generator_from_options(opts).generate_cost_partitionings(
                task_proxy, abstractions, costs, get_cp_function_from_options(opts));
    } else {
        shared_ptr<OrderGenerator> order_generator = opts.get<shared_ptr<OrderGenerator>>("orders");
        order_generator->initialize(abstractions, costs);
    }

    return make_shared<SaturatedCostPartitioningOnlineHeuristic>(
        opts,
        move(abstractions),
        move(cp_heuristics));
}

static Plugin<Evaluator> _plugin("scp_online", _parse);
}
