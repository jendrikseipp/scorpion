#include "abstraction.h"

#include "../utils/collections.h"

using namespace std;

namespace cost_saturation {
Abstraction::Abstraction(int num_operators)
    : num_operators(num_operators),
      use_general_costs(true) {
}

Abstraction::~Abstraction() {
}

pair<vector<int>, vector<int>> Abstraction::compute_goal_distances_and_saturated_costs(
    const vector<int> &costs) const {
    vector<int> h_values = compute_h_values(costs);
    vector<int> saturated_costs = compute_saturated_costs(h_values);
    return make_pair(move(h_values), move(saturated_costs));
}

const vector<int> &Abstraction::get_goal_states() const {
    return goal_states;
}

void Abstraction::release_transition_system_memory() {
    utils::release_vector_memory(active_operators);
    utils::release_vector_memory(looping_operators);
    utils::release_vector_memory(goal_states);
}
}
