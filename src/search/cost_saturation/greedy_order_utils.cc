#include "greedy_order_utils.h"

#include "types.h"

#include "../cost_saturation/greedy_order_utils.h"
#include "../plugins/plugin.h"
#include "../utils/logging.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
int compute_stolen_costs(int wanted_by_abs, int surplus_cost) {
    assert(wanted_by_abs != INF);
    assert(surplus_cost != -INF);
    if (surplus_cost == INF) {
        return 0;
    }
    // If wanted_by_abs is negative infinity, surplus_cost is positive infinity.
    assert(wanted_by_abs != -INF);

    // Both operands are finite.
    int surplus_for_rest = surplus_cost + wanted_by_abs;
    if (surplus_for_rest >= 0) {
        return max(0, wanted_by_abs - surplus_for_rest);
    } else {
        return max(wanted_by_abs, surplus_for_rest);
    }
}

int compute_costs_stolen_by_heuristic(
    const vector<int> &saturated_costs,
    const vector<int> &surplus_costs) {
    assert(saturated_costs.size() == surplus_costs.size());
    int num_operators = surplus_costs.size();
    int sum_stolen_costs = 0;
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        int stolen_costs = compute_stolen_costs(
            saturated_costs[op_id], surplus_costs[op_id]);
        assert(stolen_costs != -INF);
        sum_stolen_costs += stolen_costs;
    }
    return sum_stolen_costs;
}

static int compute_surplus_costs(
    const vector<vector<int>> &saturated_costs_by_abstraction,
    int op_id,
    int remaining_costs) {
    int num_abstractions = saturated_costs_by_abstraction.size();
    int sum_wanted = 0;
    for (int abs = 0; abs < num_abstractions; ++abs) {
        int wanted = saturated_costs_by_abstraction[abs][op_id];
        if (wanted == -INF) {
            return INF;
        } else {
            sum_wanted += wanted;
        }
    }
    assert(sum_wanted != -INF);
    if (remaining_costs == INF) {
        return INF;
    }
    return remaining_costs - sum_wanted;
}

vector<int> compute_all_surplus_costs(
    const vector<int> &costs,
    const vector<vector<int>> &saturated_costs_by_abstraction) {
    int num_operators = costs.size();
    vector<int> surplus_costs;
    surplus_costs.reserve(num_operators);
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        surplus_costs.push_back(
            compute_surplus_costs(saturated_costs_by_abstraction, op_id, costs[op_id]));
    }
    return surplus_costs;
}

double compute_score(int h, int used_costs, ScoringFunction scoring_function) {
    assert(h >= 0);
    assert(used_costs != INF);
    assert(used_costs != -INF);
    if (scoring_function == ScoringFunction::MAX_HEURISTIC) {
        return h;
    } else if (scoring_function == ScoringFunction::MIN_STOLEN_COSTS) {
        return -used_costs;
    } else if (scoring_function == ScoringFunction::MAX_HEURISTIC_PER_STOLEN_COSTS) {
        return static_cast<double>(h) / max(1, used_costs);
    } else {
        ABORT("Invalid scoring_function");
    }
}

void add_scoring_function_to_feature(plugins::Feature &feature) {
    feature.add_option<ScoringFunction>(
        "scoring_function",
        "metric for ordering abstractions/landmarks",
        "max_heuristic_per_stolen_costs");
}

static plugins::TypedEnumPlugin<ScoringFunction> _enum_plugin({
        {"max_heuristic",
         "order by decreasing heuristic value for the given state"},
        {"min_stolen_costs",
         "order by increasing sum of costs stolen from other heuristics"},
        {"max_heuristic_per_stolen_costs",
         "order by decreasing ratio of heuristic value divided by sum of stolen costs"},
    });
}
