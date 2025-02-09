#include "transition_rewirer.h"

#include "abstract_state.h"
#include "transition.h"

#include "../task_proxy.h"

#include "../task_utils/task_properties.h"

#include <algorithm>
#include <map>

using namespace std;

namespace cartesian_abstractions {
static vector<vector<FactPair>> get_preconditions_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> preconditions_by_operator;
    preconditions_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        vector<FactPair> preconditions = task_properties::get_fact_pairs(op.get_preconditions());
        sort(preconditions.begin(), preconditions.end());
        preconditions_by_operator.push_back(move(preconditions));
    }
    return preconditions_by_operator;
}

static vector<FactPair> get_postconditions(
    const OperatorProxy &op) {
    // Use map to obtain sorted postconditions.
    map<int, int> var_to_post;
    for (FactProxy fact : op.get_preconditions()) {
        var_to_post[fact.get_variable().get_id()] = fact.get_value();
    }
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        var_to_post[fact.var] = fact.value;
    }
    vector<FactPair> postconditions;
    postconditions.reserve(var_to_post.size());
    for (const pair<const int, int> &fact : var_to_post) {
        postconditions.emplace_back(fact.first, fact.second);
    }
    return postconditions;
}

static vector<vector<FactPair>> get_postconditions_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> postconditions_by_operator;
    postconditions_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        postconditions_by_operator.push_back(get_postconditions(op));
    }
    return postconditions_by_operator;
}

static int lookup_value(const vector<FactPair> &facts, int var) {
    assert(is_sorted(facts.begin(), facts.end()));
    for (const FactPair &fact : facts) {
        if (fact.var == var) {
            return fact.value;
        } else if (fact.var > var) {
            return UNDEFINED;
        }
    }
    return UNDEFINED;
}

static void remove_transitions_with_given_target(
    Transitions &transitions, int state_id) {
    auto new_end = remove_if(
        transitions.begin(), transitions.end(),
        [state_id](const Transition &t) {return t.target_id == state_id;});
    assert(new_end != transitions.end());
    transitions.erase(new_end, transitions.end());
}

static void add_transition(deque<Transitions> &incoming, deque<Transitions> &outgoing, int src, int op, int dest) {
    assert(src != dest);
    assert(find(outgoing[src].begin(), outgoing[src].end(), Transition(op, dest)) == outgoing[src].end());
    assert(find(incoming[dest].begin(), incoming[dest].end(), Transition(op, src)) == incoming[dest].end());
    outgoing[src].emplace_back(op, dest);
    incoming[dest].emplace_back(op, src);
}

static void add_loop(deque<Loops> &loops, int state_id, int op_id) {
    assert(utils::in_bounds(state_id, loops));
    loops[state_id].push_back(op_id);
}


TransitionRewirer::TransitionRewirer(const OperatorsProxy &ops)
    : preconditions_by_operator(get_preconditions_by_operator(ops)),
      postconditions_by_operator(get_postconditions_by_operator(ops)) {
}

void TransitionRewirer::rewire_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) const {
    rewire_incoming_transitions(incoming, outgoing, states, v_id, v1, v2, var);
    rewire_outgoing_transitions(incoming, outgoing, states, v_id, v1, v2, var);
}

void TransitionRewirer::rewire_incoming_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) const {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1, u->v2, or both. */
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();

    Transitions old_incoming = move(incoming[v_id]);

    unordered_set<int> updated_states;
    for (const Transition &transition : old_incoming) {
        int u_id = transition.target_id;
        bool is_new_state = updated_states.insert(u_id).second;
        if (is_new_state) {
            remove_transitions_with_given_target(outgoing[u_id], v_id);
        }
    }

    for (const Transition &transition : old_incoming) {
        int op_id = transition.op_id;
        int u_id = transition.target_id;
        const AbstractState &u = *states[u_id];
        int post = get_postcondition_value(op_id, var);
        if (post == UNDEFINED) {
            // op has no precondition and no effect on var.
            bool u_and_v1_intersect = u.domain_subsets_intersect(v1, var);
            if (u_and_v1_intersect) {
                add_transition(incoming, outgoing, u_id, op_id, v1_id);
            }
            /* If u and v1 don't intersect, we must add the other transition
               and can avoid an intersection test. */
            if (!u_and_v1_intersect || u.domain_subsets_intersect(v2, var)) {
                add_transition(incoming, outgoing, u_id, op_id, v2_id);
            }
        } else if (v1.contains(var, post)) {
            // op can only end in v1.
            add_transition(incoming, outgoing, u_id, op_id, v1_id);
        } else {
            // op can only end in v2.
            assert(v2.contains(var, post));
            add_transition(incoming, outgoing, u_id, op_id, v2_id);
        }
    }
}

void TransitionRewirer::rewire_outgoing_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) const {
    /* State v has been split into v1 and v2. Now for all transitions
       v->w we need to add transitions v1->w, v2->w, or both. */
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();

    Transitions old_outgoing = move(outgoing[v_id]);

    unordered_set<int> updated_states;
    for (const Transition &transition : old_outgoing) {
        int w_id = transition.target_id;
        bool is_new_state = updated_states.insert(w_id).second;
        if (is_new_state) {
            remove_transitions_with_given_target(incoming[w_id], v_id);
        }
    }

    for (const Transition &transition : old_outgoing) {
        int op_id = transition.op_id;
        int w_id = transition.target_id;
        const AbstractState &w = *states[w_id];
        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        if (post == UNDEFINED) {
            assert(pre == UNDEFINED);
            // op has no precondition and no effect on var.
            bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
            if (v1_and_w_intersect) {
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
            /* If v1 and w don't intersect, we must add the other transition
               and can avoid an intersection test. */
            if (!v1_and_w_intersect || v2.domain_subsets_intersect(w, var)) {
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        } else if (pre == UNDEFINED) {
            // op has no precondition, but an effect on var.
            add_transition(incoming, outgoing, v1_id, op_id, w_id);
            add_transition(incoming, outgoing, v2_id, op_id, w_id);
        } else if (v1.contains(var, pre)) {
            // op can only start in v1.
            add_transition(incoming, outgoing, v1_id, op_id, w_id);
        } else {
            // op can only start in v2.
            assert(v2.contains(var, pre));
            add_transition(incoming, outgoing, v2_id, op_id, w_id);
        }
    }
}

void TransitionRewirer::rewire_loops(
    deque<Loops> &loops, deque<Transitions> &incoming, deque<Transitions> &outgoing,
    int v_id, const AbstractState &v1, const AbstractState &v2, int var) const {
    Loops old_loops = move(loops[v_id]);
    assert(loops[v_id].empty());
    /* State v has been split into v1 and v2. Now for all self-loops
       v->v we need to add one or two of the transitions v1->v1, v1->v2,
       v2->v1 and v2->v2. */
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    for (int op_id : old_loops) {
        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        if (pre == UNDEFINED) {
            // op has no precondition on var --> it must start in v1 and v2.
            if (post == UNDEFINED) {
                // op has no effect on var --> it must end in v1 and v2.
                add_loop(loops, v1_id, op_id);
                add_loop(loops, v2_id, op_id);
            } else if (v2.contains(var, post)) {
                // op must end in v2.
                add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                add_loop(loops, v2_id, op_id);
            } else {
                // op must end in v1.
                assert(v1.contains(var, post));
                add_loop(loops, v1_id, op_id);
                add_transition(incoming, outgoing, v2_id, op_id, v1_id);
            }
        } else if (v1.contains(var, pre)) {
            // op must start in v1.
            assert(post != UNDEFINED);
            if (v1.contains(var, post)) {
                // op must end in v1.
                add_loop(loops, v1_id, op_id);
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                add_transition(incoming, outgoing, v1_id, op_id, v2_id);
            }
        } else {
            // op must start in v2.
            assert(v2.contains(var, pre));
            assert(post != UNDEFINED);
            if (v1.contains(var, post)) {
                // op must end in v1.
                add_transition(incoming, outgoing, v2_id, op_id, v1_id);
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                add_loop(loops, v2_id, op_id);
            }
        }
    }
}

int TransitionRewirer::get_precondition_value(int op_id, int var) const {
    return lookup_value(preconditions_by_operator[op_id], var);
}

int TransitionRewirer::get_postcondition_value(int op_id, int var) const {
    return lookup_value(postconditions_by_operator[op_id], var);
}

int TransitionRewirer::get_num_operators() const {
    return preconditions_by_operator.size();
}
}
