#include "cost_partitioning_collection_generator.h"

#include "abstraction.h"
#include "diversifier.h"
#include "cost_partitioning_generator.h"
#include "cost_partitioning_generator_greedy.h"
#include "utils.h"

#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_proxy.h"
#include "../task_tools.h"

#include "../options/options.h"
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
    const CostPartitioning &scp,
    const State &state) {
    vector<int> local_state_ids = get_local_state_ids(abstractions, state);
    return compute_sum_h(local_state_ids, scp) == INF;
}

void CostPartitioningCollectionGenerator::initialize(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    State initial_state = task_proxy.get_initial_state();
    options::Options greedy_opts;
    greedy_opts.set("reverse_initial_order", false);
    greedy_opts.set("scoring_function", static_cast<int>(ScoringFunction::MAX_HEURISTIC_PER_COSTS));
    greedy_opts.set("use_negative_costs", false);
    greedy_opts.set("queue_zero_ratios", true);
    greedy_opts.set("dynamic", false);
    greedy_opts.set("steepest_ascent", false);
    greedy_opts.set("max_optimization_time", 0.0);
    greedy_opts.set("random_seed", 0);
    CostPartitioningGeneratorGreedy greedy_generator(greedy_opts);
    greedy_generator.initialize(task_proxy, abstractions, costs);
    scp_for_sampling = greedy_generator.get_next_cost_partitioning(
        task_proxy, abstractions, costs, initial_state, cp_function);
    vector<int> local_state_ids = get_local_state_ids(abstractions, initial_state);
    init_h = compute_sum_h(local_state_ids, scp_for_sampling);
    sampler = utils::make_unique_ptr<RandomWalkSampler>(task_proxy, init_h, rng);
}

CostPartitionings CostPartitioningCollectionGenerator::get_cost_partitionings(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        diversifier = utils::make_unique_ptr<Diversifier>(
            task_proxy, abstractions, costs, rng);
    }

    initialize(task_proxy, abstractions, costs, cp_function);
    cp_generator->initialize(task_proxy, abstractions, costs);

    if (init_h == INF) {
        return {scp_for_sampling};
    }

    CostPartitionings cost_partitionings;
    utils::CountdownTimer timer(max_time);
    int evaluated_orders = 0;
    while (static_cast<int>(cost_partitionings.size()) < max_orders &&
           !timer.is_expired() && cp_generator->has_next_cost_partitioning()) {
        State sample = sampler->sample_state();
        // Skip dead-end samples if we have already found an order, since all
        // orders recognize the same dead ends.
        while (!cost_partitionings.empty() &&
               is_dead_end(abstractions, scp_for_sampling, sample) &&
               !timer.is_expired()) {
            sample = sampler->sample_state();
        }
        if (timer.is_expired() && !cost_partitionings.empty()) {
            break;
        }
        CostPartitioning cp = cp_generator->get_next_cost_partitioning(
            task_proxy, abstractions, costs, sample, cp_function);
        ++evaluated_orders;
        if (!diversify || (diversifier->is_diverse(cp))) {
            cost_partitionings.push_back(move(cp));
        }
    }
    cout << "Orders: " << cost_partitionings.size() << endl;
    cout << "Time for computing cost partitionings: " << timer << endl;
    return cost_partitionings;
}
}
