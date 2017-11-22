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

static bool is_dead_end(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const CostPartitioning &cp,
    const State &state) {
    vector<int> local_state_ids = get_local_state_ids(abstractions, state);
    return compute_sum_h(local_state_ids, cp) == INF;
}


void CostPartitioningCollectionGenerator::initialize(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    State initial_state = task_proxy.get_initial_state();
    scp_for_sampling = compute_cost_partitioning_for_static_order(
        task_proxy, abstractions, costs, cp_function, initial_state);
    vector<int> local_state_ids = get_local_state_ids(abstractions, initial_state);
    init_h = compute_sum_h(local_state_ids, scp_for_sampling);
    sampler = utils::make_unique_ptr<RandomWalkSampler>(task_proxy, init_h, rng);
}

vector<CostPartitionedHeuristic> CostPartitioningCollectionGenerator::get_cost_partitionings(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const vector<int> &costs,
    CPFunction cp_function,
    bool filter_zero_h_values) {
    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        diversifier = utils::make_unique_ptr<Diversifier>(
            task_proxy, abstractions, costs, cp_function, rng);
    }

    initialize(task_proxy, abstractions, costs, cp_function);
    cp_generator->initialize(task_proxy, abstractions, costs);

    if (init_h == INF) {
        return {CostPartitionedHeuristic(move(scp_for_sampling), filter_zero_h_values)};
    }

    vector<CostPartitionedHeuristic> cp_heuristics;
    utils::CountdownTimer timer(max_time);
    int evaluated_orders = 0;
    int peak_memory_without_cps = utils::get_peak_memory_in_kb();
    utils::Log() << "Start computing cost partitionings" << endl;
    while (static_cast<int>(cp_heuristics.size()) < max_orders &&
           !timer.is_expired() && cp_generator->has_next_cost_partitioning()) {
        State sample = sampler->sample_state();
        // Skip dead-end samples if we have already found an order, since all
        // orders recognize the same dead ends.
        while (!cp_heuristics.empty() &&
               is_dead_end(abstractions, scp_for_sampling, sample) &&
               !timer.is_expired()) {
            sample = sampler->sample_state();
        }
        if (timer.is_expired() && !cp_heuristics.empty()) {
            break;
        }
        CostPartitioning cp = cp_generator->get_next_cost_partitioning(
            task_proxy, abstractions, costs, sample, cp_function);
        ++evaluated_orders;
        if (!diversify || (diversifier->is_diverse(cp))) {
            cp_heuristics.emplace_back(move(cp), filter_zero_h_values);
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
