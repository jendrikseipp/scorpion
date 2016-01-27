#include "transition_system.h"

#include "abstraction.h"

#include "../utils/collections.h"

#include <cassert>

using namespace std;

namespace cegar {
TransitionSystem::TransitionSystem(
    const shared_ptr<AbstractTask> &task, const Abstraction &abstraction)
      : task_proxy(*task),
        num_states(abstraction.get_num_states()),
        operator_induces_self_loop(task_proxy.get_operators().size(), false) {

    unordered_map<AbstractState *, int> state_to_id;
    int state_id = 0;
    for (AbstractState *state : abstraction.states) {
        state_to_id[state] = state_id++;
    }

    // Store transitions.
    for (AbstractState *state : abstraction.states) {
        int start = state_to_id[state];
        for (const Arc transition : state->get_outgoing_arcs()) {
            OperatorProxy op = transition.first;
            AbstractState *succ_state = transition.second;
            transitions.emplace_back(start, op.get_id(), state_to_id[succ_state]);
        }
        for (const OperatorProxy &loop : state->get_loops()) {
            transitions.emplace_back(start, loop.get_id(), start);
            operator_induces_self_loop[loop.get_id()] = true;
        }
    }

    // Store goals.
    for (AbstractState *goal : abstraction.goals) {
        goal_indices.push_back(state_to_id[goal]);
    }
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
