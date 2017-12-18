#include "order_generator_lookahead.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/collections.h"
#include "../utils/logging.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace cost_saturation {
OrderGeneratorLookahead::OrderGeneratorLookahead(const Options &opts)
    : CostPartitioningGenerator(),
      debug(opts.get<bool>("debug")),
      num_returned_orders(0) {
}

void OrderGeneratorLookahead::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    original_costs = costs;
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        auto pair = abstraction->compute_goal_distances_and_saturated_costs(costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        h_values_by_abstraction.push_back(move(h_values));
        saturated_costs_by_abstraction.push_back(move(saturated_costs));
    }
}

double OrderGeneratorLookahead::get_fair_share(int abs1, int abs2, int op_id) const {
    int num_abstractions = saturated_costs_by_abstraction.size();
    int total_remaining_saturated_costs = 0;
    for (int abs3 = 0; abs3 < num_abstractions; ++abs3) {
        if (abs3 != abs1) {
            int abs3_saturated_costs = saturated_costs_by_abstraction[abs3][op_id];
            if (abs3_saturated_costs != -INF) {
                total_remaining_saturated_costs += abs3_saturated_costs;
            }
        }
    }

    int abs1_saturated_costs = saturated_costs_by_abstraction[abs1][op_id];
    int abs2_saturated_costs = saturated_costs_by_abstraction[abs2][op_id];
    int remaining_saturated_costs = min(
        abs2_saturated_costs, original_costs[op_id] - abs1_saturated_costs);

    if (total_remaining_saturated_costs == 0) {
        // No other abstractions wants the operator, so we can use all its costs here.
        return 1;
    }
    return static_cast<double>(abs2_saturated_costs) /
           total_remaining_saturated_costs * remaining_saturated_costs;
}

double OrderGeneratorLookahead::get_scaling_factor(int abs1, int abs2) const {
    int num_operators = original_costs.size();

    double sum_fair_share = 0.0;
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        sum_fair_share += get_fair_share(abs1, abs2, op_id);
    }

    int abs2_sum_saturated_costs = 0;
    for (int cost : saturated_costs_by_abstraction[abs2]) {
        if (cost != -INF) {
            abs2_sum_saturated_costs += cost;
        }
    }

    if (abs2_sum_saturated_costs == 0) {
        return 1;
    }
    return sum_fair_share / abs2_sum_saturated_costs;
}

double OrderGeneratorLookahead::get_scaled_h(
    int abs1, int abs2, const vector<int> &local_state_ids) const {
    int h = h_values_by_abstraction[abs2][local_state_ids[abs2]];
    return h * get_scaling_factor(abs1, abs2);
}

double OrderGeneratorLookahead::estimate_h(
    int abs1, const vector<int> &local_state_ids) const {
    double h_estimate = h_values_by_abstraction[abs1][local_state_ids[abs1]];
    int num_abstractions = local_state_ids.size();
    for (int abs2 = 0; abs2 < num_abstractions; ++abs2) {
        if (abs2 != abs1) {
            h_estimate += get_scaled_h(abs1, abs2, local_state_ids);
        }
    }
    return h_estimate;
}

Order OrderGeneratorLookahead::get_next_order(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &,
    const vector<int> &,
    const vector<int> &local_state_ids,
    bool verbose) {
    assert(compute_sum_h(local_state_ids, h_values_by_abstraction) != INF);

    utils::Timer greedy_timer;

    int num_abstractions = local_state_ids.size();
    Order order = get_default_order(num_abstractions);
    vector<double> scores;
    scores.reserve(num_abstractions);
    for (int abs = 0; abs < num_abstractions; ++abs) {
        scores.push_back(estimate_h(abs, local_state_ids));
    }
    sort(order.begin(), order.end(), [&](int abs1, int abs2) {
            return scores[abs1] > scores[abs2];
        });

    if (verbose) {
        cout << "Scores: " << scores << endl;
        utils::Log() << "Time for computing greedy order: " << greedy_timer << endl;
    }

    ++num_returned_orders;
    return order;
}


static shared_ptr<CostPartitioningGenerator> _parse(OptionParser &parser) {
    parser.add_option<bool>(
        "debug",
        "show debugging messages",
        "false");
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<OrderGeneratorLookahead>(opts);
}

static PluginShared<CostPartitioningGenerator> _plugin("lookahead", _parse);
}
