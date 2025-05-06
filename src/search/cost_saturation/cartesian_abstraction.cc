#include "cartesian_abstraction.h"

#include "types.h"

#include "../cartesian_abstractions/abstraction.h"
#include "../cartesian_abstractions/cost_saturation.h"
#include "../cartesian_abstractions/shortest_paths.h"

using namespace std;

namespace cost_saturation {
CartesianAbstraction::CartesianAbstraction(
    unique_ptr<cartesian_abstractions::Abstraction> &&abstraction_)
    : Abstraction(make_unique<CartesianAbstractionFunction>(abstraction_->extract_refinement_hierarchy())),
      abstraction(move(abstraction_)),
      looping_operators(abstraction->get_looping_operators()),
      goal_states(abstraction->get_goals().begin(), abstraction->get_goals().end()) {
    active_operators.resize(abstraction->get_num_operators(), false);
    for (int src = 0; src < abstraction->get_num_states(); ++src) {
        assert(abstraction->get_states()[src]->get_id() == src);
        for (const auto &transition : abstraction->get_outgoing_transitions(src)) {
            assert(src != transition.target_id);
            active_operators[transition.op_id] = true;
        }
    }
}

vector<int> CartesianAbstraction::compute_goal_distances(const vector<int> &costs) const {
    return cartesian_abstractions::compute_goal_distances(
        *abstraction, costs, abstraction->get_goals());
}

vector<int> CartesianAbstraction::compute_saturated_costs(
    const vector<int> &h_values) const {
    bool use_general_costs = true;
    return cartesian_abstractions::compute_saturated_costs(*abstraction, h_values, use_general_costs);
}

int CartesianAbstraction::get_num_operators() const {
    return looping_operators.size();
}

int CartesianAbstraction::get_num_states() const {
    return abstraction->get_num_states();
}

bool CartesianAbstraction::operator_is_active(int op_id) const {
    return active_operators[op_id];
}

bool CartesianAbstraction::operator_induces_self_loop(int op_id) const {
    return looping_operators[op_id];
}

void CartesianAbstraction::for_each_transition(const TransitionCallback &callback) const {
    for (int src = 0; src < get_num_states(); ++src) {
        for (const auto &transition : abstraction->get_outgoing_transitions(src)) {
            callback(Transition(src, transition.op_id, transition.target_id));
        }
    }
}

const vector<int> &CartesianAbstraction::get_goal_states() const {
    return goal_states;
}

void CartesianAbstraction::dump() const {
    abstraction->print_statistics();
}
}
