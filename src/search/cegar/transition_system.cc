#include "transition_system.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "refinement_hierarchy.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../utils/collections.h"

#include <cassert>
#include <limits>

using namespace std;

namespace cegar {
TransitionSystem::TransitionSystem(Abstraction &&abstraction)
    : num_states(abstraction.get_num_states()),
      refinement_hierarchy(abstraction.get_refinement_hierarchy()),
      get_abstract_state_id([this](const State &concrete_state) {
        return refinement_hierarchy->get_local_state_id(concrete_state);
    }),
      h_values(abstraction.get_h_values()) {
    // Store non-looping transitions.
    for (const AbstractState *state : abstraction.states) {
        int start = state->get_node()->get_state_id();
        for (const Transition &transition : state->get_outgoing_transitions()) {
            int end = transition.target->get_node()->get_state_id();
            transitions.emplace_back(start, transition.op_id, end);
        }
    }

    // Store self-loop info.
    operator_induces_self_loop = abstraction.get_operator_induces_self_loop();
    assert(!operator_induces_self_loop.empty());

    // Store goals.
    for (const AbstractState *goal : abstraction.goals) {
        goal_indices.push_back(goal->get_node()->get_state_id());
    }
}

int TransitionSystem::get_num_abstract_states() const {
    return num_states;
}

int TransitionSystem::get_abstract_state_index(const State &concrete_state) const {
    return get_abstract_state_id(concrete_state);
}

bool TransitionSystem::is_dead_end(const State &concrete_state) const {
    return h_values[get_abstract_state_index(concrete_state)] ==
           numeric_limits<int>::max();
}

bool TransitionSystem::induces_self_loop(int op_id) const {
    assert(utils::in_bounds(op_id, operator_induces_self_loop));
    return operator_induces_self_loop[op_id];
}

const vector<int> &TransitionSystem::get_goal_indices() const {
    assert(!goal_indices.empty());
    return goal_indices;
}

const vector<ExplicitTransition> &TransitionSystem::get_transitions() const {
    assert(!transitions.empty());
    return transitions;
}

void TransitionSystem::release_memory() {
    utils::release_vector_memory(operator_induces_self_loop);
    utils::release_vector_memory(transitions);
    utils::release_vector_memory(goal_indices);
}
}
