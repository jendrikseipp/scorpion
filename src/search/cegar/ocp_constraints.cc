#include "ocp_constraints.h"

#include "abstraction.h"

#include "../lp/lp_solver.h"

#include <cassert>

using namespace std;

namespace cegar {
OCPConstraints::OCPConstraints(
    const TaskProxy &subtask_proxy, const Abstraction &abstraction)
      : num_transitions(0),
        num_goals(abstraction.goals.size()),
        init_offset(subtask_proxy.get_operators().size()),
        goals_offset(init_offset + 1),
        transitions_offset(goals_offset + num_goals),
        states(abstraction.states),
        goals(abstraction.goals) {

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
    }

    // \sum(s' \in G) G_{s'} = 1
    ocp_constraints.emplace_back(1, 1);
    lp::LPConstraint &constraint = ocp_constraints.back();
    for (int i = 0; i < num_goals; ++i) {
        constraint.insert(goals_offset + i, 1);
    }

    /*     Y_o = \sum_{t \in T, t labeled with o} T_t
       <=> Y_o - \sum_{t \in T, t labeled with o} T_t = 0 */
    for (OperatorProxy op : subtask_proxy.get_operators()) {
        ocp_constraints.emplace_back(0, 0);
        lp::LPConstraint &constraint = ocp_constraints.back();
        constraint.insert(op.get_id(), 1);
        for (int transition_id : operator_to_transitions[op.get_id()]) {
            constraint.insert(transitions_offset + transition_id, -1);
        }
    }

    /* \sum_{t \in T, t ends in s'} T_t - \sum{t \in T, t starts in s'} T_t
        - G_{s'}[s' \in G] - I[s' = \alpha(s)] = 0 */
    State initial_state = subtask_proxy.get_initial_state();
    for (AbstractState *abstract_state : states) {
        ocp_constraints.emplace_back(0, 0);
        lp::LPConstraint &constraint = ocp_constraints.back();
        for (int transition_id : state_to_incoming_transitions[abstract_state]) {
            constraint.insert(transition_id, 1);
        }
        for (int transition_id : state_to_outgoing_transitions[abstract_state]) {
            constraint.insert(transition_id, -1);
        }
        // Use O(log n) inclusion test first. Only do O(n) lookup for goal states.
        if (goals.count(abstract_state) != 0) {
            int goal_state_id = 0;
            for (AbstractState *goal : goals) {
                if (goal == abstract_state) {
                    constraint.insert(goals_offset + goal_state_id, -1);
                    break;
                }
            }
        }
        if (abstract_state->includes(initial_state)) {
            constraint.insert(init_offset, 1);
        }
    }
}

void OCPConstraints::initialize_variables(
    const std::shared_ptr<AbstractTask>,
    vector<lp::LPVariable> &variables,
    double infinity) {

    // -inf <= I <= inf
    assert(init_offset == variables.size());
    variables.emplace_back(-infinity, infinity, 0);

    // 0 <= G_{s'} <= inf for all s' \in G
    assert(goals_offset == variables.size());
    for (int i = 0; i < num_goals; ++i) {
        variables.emplace_back(0, infinity, 0);
    }

    // 0 <= T_t <= inf for all t \in T
    assert(transitions_offset == variables.size());
    for (int i = 0; i < num_transitions; ++i) {
        variables.emplace_back(0, infinity, 0);
    }
}

void OCPConstraints::initialize_constraints(
    const std::shared_ptr<AbstractTask>,
    vector<lp::LPConstraint> &constraints,
    double) {
    constraints.insert(constraints.end(), ocp_constraints.begin(), ocp_constraints.end());
}

bool OCPConstraints::update_constraints(const State &, lp::LPSolver &) {
    return false;
}
}
