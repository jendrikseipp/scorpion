#include "abstraction.h"

using namespace std;

namespace cost_saturation {
Abstraction::Abstraction()
    : use_general_costs(true) {
}

pair<vector<int>, vector<int>> Abstraction::compute_goal_distances_and_saturated_costs(
    const vector<int> &costs) const {
    vector<int> h_values = compute_h_values(costs);
    vector<int> saturated_costs = compute_saturated_costs(h_values);
    return make_pair(move(h_values), move(saturated_costs));
}
}

