#include "abstraction.h"

#include "../utils/collections.h"

using namespace std;

namespace cost_saturation {
Abstraction::Abstraction(int num_operators)
    : num_operators(num_operators),
      use_general_costs(true) {
}

std::vector<int> Abstraction::compute_h_values(
    const std::vector<int> &costs) const {
    (void) costs;
    return {};
}

vector<int> Abstraction::compute_saturated_costs(
    const vector<int> &h_values) const {
    const int min_cost = use_general_costs ? -INF : 0;

    vector<int> saturated_costs(num_operators, min_cost);

    /* To prevent negative cost cycles we ensure that all operators
       inducing self-loops have non-negative costs. */
    if (use_general_costs) {
        for (int op_id : looping_operators) {
            saturated_costs[op_id] = 0;
        }
    }

    for (size_t state = 0; state < state_changing_transitions.size(); ++state) {
        assert(utils::in_bounds(state, h_values));
        int h = h_values[state];
        assert(h != INF);

        for (const Transition &transition : state_changing_transitions[state]) {
            int op_id = transition.op;
            int successor = transition.target;
            assert(utils::in_bounds(successor, h_values));
            int succ_h = h_values[successor];
            assert(succ_h != INF);

            const int needed = h - succ_h;
            assert(needed >= 0);
            saturated_costs[op_id] = max(saturated_costs[op_id], needed);
        }
    }
    return saturated_costs;
}

pair<vector<int>, vector<int>> Abstraction::compute_h_values_and_saturated_costs(
    const vector<int> &costs) {
    vector<int> h_values = compute_h_values(costs);
    vector<int> saturated_costs = compute_saturated_costs(h_values);
    return make_pair(move(h_values), move(saturated_costs));
}
}

