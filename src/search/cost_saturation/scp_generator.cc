#include "scp_generator.h"

#include "abstraction.h"
#include "diversifier.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
vector<vector<int>> compute_saturated_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
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


SCPGenerator::SCPGenerator(const Options &opts)
    : max_orders(opts.get<int>("max_orders")),
      max_time(opts.get<double>("max_time")),
      diversify(opts.get<bool>("diversify")) {
}

void SCPGenerator::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &,
    const vector<int> &) {
    // Do nothing by default.
}

CostPartitionings SCPGenerator::get_cost_partitionings(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    initialize(task_proxy, abstractions, costs);

    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        diversifier = utils::make_unique_ptr<Diversifier>(
            task_proxy, abstractions, costs);
    }
    CostPartitionings cost_partitionings;
    utils::CountdownTimer timer(max_time);
    int evaluated_orders = 0;
    while (static_cast<int>(cost_partitionings.size()) < max_orders &&
           !timer.is_expired() && has_next_cost_partitioning()) {

        CostPartitioning scp = get_next_cost_partitioning(
            task_proxy, abstractions, costs);
        ++evaluated_orders;
        if (!diversify || diversifier->is_diverse(scp)) {
            cost_partitionings.push_back(move(scp));
        }
    }
    cout << "Total evaluated orders: " << evaluated_orders << endl;
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
        "only keep diverse orders",
        "true");
}


static PluginTypePlugin<SCPGenerator> _type_plugin(
    "SCPGenerator",
    "Saturated cost partitioning generator.");
}
