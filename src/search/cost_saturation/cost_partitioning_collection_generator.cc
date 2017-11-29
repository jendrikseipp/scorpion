#include "cost_partitioning_collection_generator.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
#include "cost_partitioning_generator.h"
#include "diversifier.h"
#include "utils.h"

#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_proxy.h"
#include "../task_tools.h"

#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
CostPartitioningCollectionGenerator::CostPartitioningCollectionGenerator(
    const shared_ptr<CostPartitioningGenerator> &cp_generator,
    int max_orders,
    double max_time,
    bool diversify,
    const shared_ptr<utils::RandomNumberGenerator> &rng)
    : cp_generator(cp_generator),
      max_orders(max_orders),
      max_time(max_time),
      diversify(diversify),
      rng(rng) {
}

CostPartitioningCollectionGenerator::~CostPartitioningCollectionGenerator() {
}

vector<CostPartitionedHeuristic> CostPartitioningCollectionGenerator::get_cost_partitionings(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        diversifier = utils::make_unique_ptr<Diversifier>(
            task_proxy, abstractions, costs, cp_function, rng);
    }

    State initial_state = task_proxy.get_initial_state();
    CostPartitionedHeuristic scp_for_sampling = compute_cost_partitioning_for_static_order(
        task_proxy, abstractions, costs, cp_function, initial_state);
    function<int (const State &state)> sampling_heuristic =
        [&abstractions, &scp_for_sampling](const State &state) {
            vector<int> local_state_ids = get_local_state_ids(abstractions, state);
            return scp_for_sampling.compute_heuristic(local_state_ids);
        };
    DeadEndDetector is_dead_end = [&sampling_heuristic](const State &state) {
                                      return sampling_heuristic(state) == INF;
                                  };
    int init_h = sampling_heuristic(initial_state);
    RandomWalkSampler sampler(task_proxy, init_h, rng, is_dead_end);

    if (init_h == INF) {
        return {
                scp_for_sampling
        };
    }

    cp_generator->initialize(task_proxy, abstractions, costs);

    vector<CostPartitionedHeuristic> cp_heuristics;
    utils::CountdownTimer timer(max_time);
    int evaluated_orders = 0;
    int peak_memory_without_cps = utils::get_peak_memory_in_kb();
    utils::Log() << "Start computing cost partitionings" << endl;
    while (static_cast<int>(cp_heuristics.size()) < max_orders &&
           !timer.is_expired() && cp_generator->has_next_cost_partitioning()) {
        State sample = sampler.sample_state();
        assert(sample == initial_state || !is_dead_end(sample));
        if (timer.is_expired() && !cp_heuristics.empty()) {
            break;
        }
        CostPartitionedHeuristic cp = cp_generator->get_next_cost_partitioning(
            task_proxy, abstractions, costs, sample, cp_function);
        ++evaluated_orders;
        if (!diversify || (diversifier->is_diverse(cp))) {
            cp_heuristics.push_back(move(cp));
        }
    }
    int peak_memory_with_cps = utils::get_peak_memory_in_kb();
    utils::Log() << "Orders: " << cp_heuristics.size() << endl;
    utils::Log() << "Time for computing cost partitionings: " << timer << endl;
    utils::Log() << "Memory for cost partitionings: "
                 << peak_memory_with_cps - peak_memory_without_cps << " KB" << endl;
    return cp_heuristics;
}
}
