#include "ocp_constraints.h"

#include "abstraction.h"

#include "../lp/lp_solver.h"

#include <cassert>

using namespace std;

namespace cegar {
OCPConstraints::OCPConstraints(const Abstraction &abstraction)
      : num_transitions(0),
        num_goals(abstraction.goals.size()),
        states(abstraction.states),
        initial_state(abstraction.init),
        goals(abstraction.goals) {

    // Store transition system.
    for (AbstractState *abstract_state : states) {
        for (const Arc transition : abstract_state->get_outgoing_arcs()) {
            OperatorProxy op = transition.first;
            AbstractState *succ_state = transition.second;
            // TODO: Consider self-loops.
            operator_to_transitions[op.get_id()].push_back(num_transitions);
            state_to_incoming_transitions[succ_state].push_back(num_transitions);
            state_to_outgoing_transitions[abstract_state].push_back(num_transitions);
            ++num_transitions;
        }
    }
}

void OCPConstraints::initialize_variables(
    const std::shared_ptr<AbstractTask>,
    vector<lp::LPVariable> &variables,
    double infinity) {

    // 0 <= G_{s'} <= inf for all s' \in G
    goals_offset = variables.size();
    for (int i = 0; i < num_goals; ++i) {
        variables.emplace_back(0, infinity, 0);
    }

    // 0 <= T_t <= inf for all t \in T
    transitions_offset = variables.size();
    for (int i = 0; i < num_transitions; ++i) {
        variables.emplace_back(0, infinity, 0);
    }
}

void OCPConstraints::initialize_constraints(
    const std::shared_ptr<AbstractTask> task,
    vector<lp::LPConstraint> &constraints,
    double infinity) {

    // \sum(s' \in G) G_{s'} >= 1
    constraints.emplace_back(1, infinity);
    lp::LPConstraint &constraint = constraints.back();
    for (int i = 0; i < num_goals; ++i) {
        constraint.insert(goals_offset + i, 1);
    }

    /*     Y_o = \sum_{t \in T, t labeled with o} T_t
       <=> Y_o - \sum_{t \in T, t labeled with o} T_t >= 0 */
    TaskProxy task_proxy(*task);
    for (OperatorProxy op : task_proxy.get_operators()) {
        constraints.emplace_back(0, infinity);
        lp::LPConstraint &constraint = constraints.back();
        constraint.insert(op.get_id(), 1);
        for (int transition_id : operator_to_transitions[op.get_id()]) {
            constraint.insert(transitions_offset + transition_id, -1);
        }
    }

    /* \sum_{t \in T, t ends in s'} T_t - \sum{t \in T, t starts in s'}
    T_t - G_{s'}[s' \in G] + I[s' = \alpha(s)] >= 0

    Since we need I only for the abstract state corresponding to s and
    I is unrestricted, we don't introduce I and add the constraint only
    for all other abstract states.
    */
    for (AbstractState *abstract_state : states) {
        if (abstract_state == initial_state) {
            continue;
        }
        constraints.emplace_back(0, infinity);
        lp::LPConstraint &constraint = constraints.back();
        for (int transition_id : state_to_incoming_transitions[abstract_state]) {
            constraint.insert(transitions_offset + transition_id, 1);
        }
        for (int transition_id : state_to_outgoing_transitions[abstract_state]) {
            constraint.insert(transitions_offset + transition_id, -1);
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
            ++goal_state_id;
        }
    }
    std::unordered_map<int, std::vector<int>>().swap(operator_to_transitions);
    std::unordered_map<AbstractState *, std::vector<int>>().swap(state_to_incoming_transitions);
    std::unordered_map<AbstractState *, std::vector<int>>().swap(state_to_outgoing_transitions);
    std::unordered_set<AbstractState *>().swap(states);
    std::unordered_set<AbstractState *>().swap(goals);
}

bool OCPConstraints::update_constraints(const State &, lp::LPSolver &) {
    /* Currently, we compute the cost partitioning only once for the
       initial state and use it for the whole search. If we ever want
       to make this state-dependent, we must enable the previously
       disabled constraint and disable the one corresponding to state. */
    return false;
}
}
