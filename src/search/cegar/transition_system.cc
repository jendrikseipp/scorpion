#include "transition_system.h"

#include <cassert>

using namespace std;

namespace cegar {
TransitionSystem::TransitionSystem(const shared_ptr<AbstractTask> &task, const Abstraction &)
      : task_proxy(*task) {
    /*
    // Store transition system.
    for (AbstractState *abstract_state : states) {
        for (const Arc transition : abstract_state->get_outgoing_arcs()) {
            OperatorProxy op = transition.first;
            AbstractState *succ_state = transition.second;
            operator_to_transitions[op.get_id()].push_back(num_transitions);
            state_to_incoming_transitions[succ_state].push_back(num_transitions);
            state_to_outgoing_transitions[abstract_state].push_back(num_transitions);
            ++num_transitions;
        }
    }*/
}

bool TransitionSystem::induces_self_loop(int op_id) const {
    assert(utils::in_bounds(op_id, operator_induces_self_loop));
    return operator_induces_self_loop[op_id];
}
}
