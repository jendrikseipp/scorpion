#include "cost_partitioning_heuristic_collection_generator.h"

#include "cost_partitioning_heuristic.h"
#include "diversifier.h"
#include "order_generator.h"
#include "order_optimizer.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../task_utils/sampling.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng_options.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
static vector<vector<int>> sample_states_and_return_abstract_state_ids(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const sampling::RandomWalkSampler &sampler,
    int num_samples,
    int init_h,
    const DeadEndDetector &is_dead_end,
    double max_sampling_time) {
    assert(num_samples >= 1);
    utils::CountdownTimer sampling_timer(max_sampling_time);
    utils::g_log << "Start sampling" << endl;
    vector<vector<int>> abstract_state_ids_by_sample;
    abstract_state_ids_by_sample.push_back(
        get_abstract_state_ids(abstractions, task_proxy.get_initial_state()));
    while (static_cast<int>(abstract_state_ids_by_sample.size()) < num_samples
           && !sampling_timer.is_expired()) {
        abstract_state_ids_by_sample.push_back(
            get_abstract_state_ids(abstractions, sampler.sample_state(init_h, is_dead_end)));
    }
    utils::g_log << "Samples: " << abstract_state_ids_by_sample.size() << endl;
    utils::g_log << "Sampling time: " << sampling_timer.get_elapsed_time() << endl;
    return abstract_state_ids_by_sample;
}


CostPartitioningHeuristicCollectionGenerator::CostPartitioningHeuristicCollectionGenerator(
    const shared_ptr<OrderGenerator> &order_generator,
    int max_orders,
    int max_size_kb,
    double max_time,
    bool diversify,
    int num_samples,
    double max_optimization_time,
    int random_seed)
    : order_generator(order_generator),
      max_orders(max_orders),
      max_size_kb(max_size_kb),
      max_time(max_time),
      diversify(diversify),
      num_samples(num_samples),
      max_optimization_time(max_optimization_time),
      rng(utils::get_rng(random_seed)) {
    if (max_orders == INF && max_size_kb == INF && max_time == numeric_limits<double>::infinity()) {
        cerr << "max_orders, max_size and max_time cannot all be infinity" << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}

vector<CostPartitioningHeuristic>
CostPartitioningHeuristicCollectionGenerator::generate_cost_partitionings(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const vector<int> &costs,
    const CPFunction &cp_function) const {
    utils::Log log(utils::Verbosity::NORMAL);
    utils::CountdownTimer timer(max_time);

    State initial_state = task_proxy.get_initial_state();

    order_generator->initialize(abstractions, costs);

    vector<int> abstract_state_ids_for_init = get_abstract_state_ids(
        abstractions, initial_state);
    Order order_for_init = order_generator->compute_order_for_state(
        abstract_state_ids_for_init, true);
    vector<int> remaining_costs = costs;
    CostPartitioningHeuristic cp_for_init = cp_function(
        abstractions, order_for_init, remaining_costs, abstract_state_ids_for_init);
    int init_h = cp_for_init.compute_heuristic(abstract_state_ids_for_init);

    if (init_h == INF) {
        log << "Initial state is unsolvable." << endl;
        return {
            cp_for_init
        };
    }

    sampling::RandomWalkSampler sampler(task_proxy, *rng);
    DeadEndDetector is_dead_end =
        [&abstractions, &cp_for_init](const State &state) {
            return cp_for_init.compute_heuristic(
                get_abstract_state_ids(abstractions, state)) == INF;
        };

    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        double max_sampling_time = timer.get_remaining_time();
        diversifier = utils::make_unique_ptr<Diversifier>(
            sample_states_and_return_abstract_state_ids(
                task_proxy, abstractions, sampler, num_samples, init_h, is_dead_end, max_sampling_time));
    }

    log << "Start computing cost partitionings" << endl;
    vector<CostPartitioningHeuristic> cp_heuristics;
    int evaluated_orders = 0;
    int size_kb = 0;
    while (static_cast<int>(cp_heuristics.size()) < max_orders &&
           (!timer.is_expired() || cp_heuristics.empty()) &&
           (size_kb < max_size_kb)) {
        bool is_first_order = (evaluated_orders == 0);

        vector<int> abstract_state_ids;
        Order order;
        CostPartitioningHeuristic cp_heuristic;
        if (is_first_order) {
            // Use initial state as first sample.
            abstract_state_ids = abstract_state_ids_for_init;
            order = order_for_init;
            cp_heuristic = cp_for_init;
        } else {
            abstract_state_ids = get_abstract_state_ids(
                abstractions, sampler.sample_state(init_h, is_dead_end));
            order = order_generator->compute_order_for_state(
                abstract_state_ids, false);
            remaining_costs = costs;
            cp_heuristic = cp_function(abstractions, order, remaining_costs, abstract_state_ids);
        }

        // Optimize order.
        double optimization_time = min(
            static_cast<double>(timer.get_remaining_time()), max_optimization_time);
        if (optimization_time > 0) {
            utils::CountdownTimer opt_timer(optimization_time);
            int incumbent_h_value = cp_heuristic.compute_heuristic(abstract_state_ids);
            optimize_order_with_hill_climbing(
                cp_function, opt_timer, abstractions, costs, abstract_state_ids, order,
                cp_heuristic, incumbent_h_value, is_first_order);
            if (is_first_order) {
                log << "Time for optimizing order: " << opt_timer.get_elapsed_time()
                    << endl;
            }
        }

        // If diversify=true, only add order if it improves upon previously
        // added orders.
        if (!diversifier || diversifier->is_diverse(cp_heuristic)) {
            size_kb += cp_heuristic.estimate_size_in_kb();
            cp_heuristics.push_back(move(cp_heuristic));
            if (diversifier) {
                log << "Average finite h-value for " << num_samples
                    << " samples after " << timer.get_elapsed_time()
                    << " of diversification: "
                    << diversifier->compute_avg_finite_sample_h_value()
                    << endl;
            }
        }

        ++evaluated_orders;
    }

    log << "Evaluated orders: " << evaluated_orders << endl;
    log << "Cost partitionings: " << cp_heuristics.size() << endl;
    log << "Time for computing cost partitionings: " << timer.get_elapsed_time()
        << endl;
    log << "Estimated heuristic size: " << size_kb << " KiB" << endl;
    return cp_heuristics;
}
}
