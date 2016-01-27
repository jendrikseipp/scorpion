#include "transition_system.h"

#include "../task_proxy.h"

#include <cassert>

using namespace std;

namespace cegar {
TransitionSystem::TransitionSystem(const Abstraction &) {
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

bool TransitionSystem::induces_self_loop(const OperatorProxy &op) const {
    assert(utils::in_bounds(op.get_id(), operator_induces_self_loop));
    return operator_induces_self_loop[op.get_id()];
}
}
