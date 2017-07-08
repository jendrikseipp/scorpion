#include "cost_partitioning_generator.h"

#include "abstraction.h"
#include "diversifier.h"
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
CostPartitioning compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool debug) {
    assert(abstractions.size() == order.size());
    vector<int> remaining_costs = costs;
    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
        auto pair = abstraction.compute_goal_distances_and_saturated_costs(
            remaining_costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        if (debug) {
            cout << "h-values: ";
            print_indexed_vector(h_values);
            cout << "saturated costs: ";
            print_indexed_vector(saturated_costs);
        }
        h_values_by_abstraction[pos] = move(h_values);
        reduce_costs(remaining_costs, saturated_costs);
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return h_values_by_abstraction;
}


CostPartitioningGenerator::CostPartitioningGenerator(const Options &opts)
    : max_orders(opts.get<int>("max_orders")),
      max_time(opts.get<double>("max_time")),
      diversify(opts.get<bool>("diversify")),
      rng(utils::parse_rng_from_options(opts)) {
}

CostPartitioningGenerator::~CostPartitioningGenerator() {
}

void CostPartitioningGenerator::initialize(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    successor_generator = utils::make_unique_ptr<SuccessorGenerator>(task_proxy);
    average_operator_costs = get_average_operator_cost(task_proxy);
    initial_state = utils::make_unique_ptr<State>(task_proxy.get_initial_state());

    vector<int> default_order = get_default_order(abstractions.size());
    CostPartitioning scp_for_default_order =
        compute_saturated_cost_partitioning(abstractions, default_order, costs);

    std::function<int (const State &state)> default_order_heuristic =
        [&abstractions, &scp_for_default_order](const State &state) {
            vector<int> local_state_ids = get_local_state_ids(abstractions, state);
            return compute_sum_h(local_state_ids, scp_for_default_order);
        };

    init_h = default_order_heuristic(*initial_state);
    cout << "Initial h value for default order: " << init_h << endl;
}

CostPartitionings CostPartitioningGenerator::get_cost_partitionings(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        diversifier = utils::make_unique_ptr<Diversifier>(
            task_proxy, abstractions, costs, *rng);
    }

    initialize(task_proxy, abstractions, costs);

    CostPartitionings cost_partitionings;
    utils::CountdownTimer timer(max_time);
    int evaluated_orders = 0;
    while (static_cast<int>(cost_partitionings.size()) < max_orders &&
           !timer.is_expired() && has_next_cost_partitioning()) {
        // Always start with the initial state before turning to samples.
        State sample = *initial_state;
        if (!cost_partitionings.empty()) {
            sample = sample_state_with_random_walk(
                *initial_state,
                *successor_generator,
                init_h,
                average_operator_costs,
                *rng);
        }
        CostPartitioning scp = get_next_cost_partitioning(
            task_proxy, abstractions, costs, sample, cp_function);
        ++evaluated_orders;
        if (!diversify || (diversifier->is_diverse(scp))) {
            cost_partitionings.push_back(move(scp));
        }
    }
    cout << "Orders: " << cost_partitionings.size() << endl;
    cout << "Time for computing cost partitionings: " << timer << endl;
    return cost_partitionings;
}


void add_common_scp_generator_options_to_parser(OptionParser &parser) {
    parser.add_option<int>(
        "max_orders",
        "maximum number of abstraction orders",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time for finding cost partitionings",
        "10",
        Bounds("0", "infinity"));
    parser.add_option<bool>(
        "diversify",
        "keep orders that improve the portfolio's heuristic value for any of the samples",
        "true");
    utils::add_rng_options(parser);
}


static PluginTypePlugin<CostPartitioningGenerator> _type_plugin(
    "CostPartitioningGenerator",
    "Cost partitioning generator.");
}
