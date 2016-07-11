#include "transition_system.h"

#include "abstraction.h"
#include "abstract_state.h"

#include "../global_state.h"
#include "../globals.h"

#include "../utils/collections.h"

#include <cassert>

using namespace std;

namespace cegar {
TransitionSystem::TransitionSystem(
    const shared_ptr<AbstractTask> &task, Abstraction &&abstraction)
      : task(task),
        task_proxy(*task),
        num_states(abstraction.get_num_states()),
        refinement_hierarchy(abstraction.get_refinement_hierarchy()),
        operator_induces_self_loop(task_proxy.get_operators().size(), false) {

    unordered_map<AbstractState *, int> state_to_id;
    int state_id = 0;
    for (AbstractState *state : abstraction.states) {
        state_to_id[state] = state_id++;
    }

    for (AbstractState *state : abstraction.states) {
        node_to_state_id[state->node] = state_to_id[state];
    }

    // Store transitions.
    for (AbstractState *state : abstraction.states) {
        int start = state_to_id[state];
        for (const Transition &transition : state->get_outgoing_transitions()) {
            transitions.emplace_back(
                start, transition.op_id, state_to_id[transition.target]);
        }
        for (int op_id : state->get_loops()) {
            operator_induces_self_loop[op_id] = true;
        }
    }

    // Store goals.
    for (AbstractState *goal : abstraction.goals) {
        goal_indices.push_back(state_to_id[goal]);
    }

    // Store heuristic values.
    for (AbstractState *state : abstraction.states) {
        h_values.push_back(state->get_h_value());
    }
}

int TransitionSystem::get_abstract_state_index(
    const State &concrete_state) const {
    State abstract_state = task_proxy.convert_ancestor_state(concrete_state);
    Node *node = refinement_hierarchy->get_node(abstract_state);
    return node_to_state_id.at(node);
}

bool TransitionSystem::is_dead_end(const State &concrete_state) const {
    State abstract_state = task_proxy.convert_ancestor_state(concrete_state);
    Node *node = refinement_hierarchy->get_node(abstract_state);
    return h_values[node_to_state_id.at(node)] == std::numeric_limits<int>::max();
}

bool TransitionSystem::induces_self_loop(int op_id) const {
    assert(utils::in_bounds(op_id, operator_induces_self_loop));
    return operator_induces_self_loop[op_id];
}

void TransitionSystem::release_memory() {
    utils::release_vector_memory(operator_induces_self_loop);
    utils::release_vector_memory(transitions);
    utils::release_vector_memory(goal_indices);
}
}
