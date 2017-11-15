#include "cost_partitioning_collection_generator.h"

#include "abstraction.h"
#include "diversifier.h"
#include "cost_partitioning_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_proxy.h"
#include "../task_tools.h"

#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/rng_options.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
CostPartitioningCollectionGenerator::CostPartitioningCollectionGenerator(const Options &opts)
    : cp_generator(opts.get<shared_ptr<CostPartitioningGenerator>>("cost_partitioning_generator")),
      max_orders(opts.get<int>("max_orders")),
      max_time(opts.get<double>("max_time")),
      diversify(opts.get<bool>("diversify")),
      rng(utils::parse_rng_from_options(opts)) {
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
    const vector<int> &costs) {
    State initial_state = task_proxy.get_initial_state();
    vector<int> default_order = get_default_order(abstractions.size());
    scp_for_sampling =
        compute_saturated_cost_partitioning(abstractions, default_order, costs);
    vector<int> local_state_ids = get_local_state_ids(abstractions, initial_state);
    init_h = compute_sum_h(local_state_ids, scp_for_sampling);
    cout << "Initial h value for default order: " << init_h << endl;
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

    initialize(task_proxy, abstractions, costs);
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


static PluginTypePlugin<CostPartitioningCollectionGenerator> _type_plugin(
    "CostPartitioningCollectionGenerator",
    "Cost partitioning collection generator.");
}
