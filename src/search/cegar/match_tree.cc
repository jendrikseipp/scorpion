#include "match_tree.h"

#include "abstract_state.h"
#include "refinement_hierarchy.h"
#include "transition.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <algorithm>
#include <map>

using namespace std;

namespace cegar {
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

static void remove_transition(
    Transitions &transitions, int op_id, int state_id) {
    auto new_end = remove(
        transitions.begin(), transitions.end(), Transition(op_id, state_id));
    assert(new_end != transitions.end());
    transitions.erase(new_end, transitions.end());
    // TODO: use swap and pop.
}


MatchTree::MatchTree(
    const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
    bool debug)
    : preconditions_by_operator(get_preconditions_by_operator(ops)),
      postconditions_by_operator(get_postconditions_by_operator(ops)),
      refinement_hierarchy(refinement_hierarchy),
      num_non_loops(0),
      num_loops(0),
      debug(debug) {
    add_loops_in_trivial_abstraction();
}

int MatchTree::get_precondition_value(int op_id, int var) const {
    return lookup_value(preconditions_by_operator[op_id], var);
}

int MatchTree::get_postcondition_value(int op_id, int var) const {
    return lookup_value(postconditions_by_operator[op_id], var);
}

void MatchTree::enlarge_vectors_by_one() {
    int new_num_states = get_num_states() + 1;
    outgoing.resize(new_num_states);
    incoming.resize(new_num_states);
    loops.resize(new_num_states);
}

void MatchTree::add_loops_in_trivial_abstraction() {
    assert(get_num_states() == 0);
    enlarge_vectors_by_one();
    int init_id = 0;
    for (int i = 0; i < get_num_operators(); ++i) {
        add_loop(init_id, i);
    }
}

void MatchTree::add_transition(int src_id, int op_id, int target_id) {
    if (src_id == target_id) {
        add_loop(src_id, op_id);
    } else {
        outgoing[src_id].emplace_back(op_id, target_id);
        incoming[target_id].emplace_back(op_id, src_id);
        ++num_non_loops;
    }
}

void MatchTree::add_loop(int state_id, int op_id) {
    loops[state_id].push_back(op_id);
    ++num_loops;
}

void MatchTree::rewire_incoming_transitions(
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1, u->v2, or both. */
    NodeID v1_id = v1.get_node_id();
    NodeID v2_id = v2.get_node_id();

    Transitions old_transitions;
    swap(old_transitions, incoming[v_id]);
    num_non_loops -= old_transitions.size();

    for (const Transition &transition : old_transitions) {
        int op_id = transition.op_id;
        int u_id = transition.target_id;
        // TODO: only remove invalid transitions.
        remove_transition(outgoing[u_id], op_id, v_id);
        const AbstractState &u = *states[refinement_hierarchy.nodes[u_id].get_state_id()];
        int post = get_postcondition_value(op_id, var);
        if (post == UNDEFINED) {
            // op has no precondition and no effect on var.
            bool u_and_v1_intersect = u.domain_subsets_intersect(v1, var);
            bool u_and_v2_intersect = u.domain_subsets_intersect(v2, var);
            if (u_and_v1_intersect && u_and_v2_intersect) {
                add_transition(u_id, op_id, v_id);
            } else if (u_and_v1_intersect) {
                add_transition(u_id, op_id, v1_id);
            } else {
                assert(u_and_v2_intersect);
                add_transition(u_id, op_id, v2_id);
            }
        } else if (v1.contains(var, post)) {
            // op can only end in v1.
            add_transition(u_id, op_id, v1_id);
        } else {
            // op can only end in v2.
            assert(v2.contains(var, post));
            add_transition(u_id, op_id, v2_id);
        }
    }
}

void MatchTree::rewire_outgoing_transitions(
    const AbstractStates &states, NodeID v_id,
    const AbstractState &v1, const AbstractState &v2, int var) {
    /* State v has been split into v1 and v2. Now for all transitions
       v->w we need to add transitions v1->w, v2->w, or both. */
    NodeID v1_id = v1.get_node_id();
    NodeID v2_id = v2.get_node_id();

    Transitions old_transitions;
    swap(old_transitions, outgoing[v_id]);
    num_non_loops -= old_transitions.size();

    for (const Transition &transition : old_transitions) {
        int op_id = transition.op_id;
        int w_id = transition.target_id;
        // TODO: only remove invalid transitions.
        remove_transition(incoming[w_id], op_id, v_id);
        const AbstractState &w = *states[refinement_hierarchy.nodes[w_id].get_state_id()];
        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        if (post == UNDEFINED) {
            assert(pre == UNDEFINED);
            // op has no precondition and no effect on var.
            // TODO: compute this lazily.
            bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
            bool v2_and_w_intersect = v2.domain_subsets_intersect(w, var);
            // If both transitions exist, store a single transition from v.
            if (v1_and_w_intersect && v2_and_w_intersect) {
                add_transition(v_id, op_id, w_id);
            } else if (v1_and_w_intersect) {
                add_transition(v1_id, op_id, w_id);
            } else {
                assert(v2_and_w_intersect);
                add_transition(v2_id, op_id, w_id);
            }
        } else if (pre == UNDEFINED) {
            // op has no precondition, but an effect on var.
            add_transition(v_id, op_id, w_id);
        } else if (v1.contains(var, pre)) {
            // op can only start in v1.
            add_transition(v1_id, op_id, w_id);
        } else {
            // op can only start in v2.
            assert(v2.contains(var, pre));
            add_transition(v2_id, op_id, w_id);
        }
    }
}

void MatchTree::rewire_loops(
    NodeID v_id, const AbstractState &v1, const AbstractState &v2, int var) {
    /* State v has been split into v1 and v2. Now for all self-loops
       v->v we need to add one or two of the transitions v1->v1, v1->v2,
       v2->v1 and v2->v2. */
    NodeID v1_id = v1.get_node_id();
    NodeID v2_id = v2.get_node_id();

    Loops old_loops;
    swap(old_loops, loops[v_id]);
    num_loops -= old_loops.size();

    for (int op_id : old_loops) {
        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        if (pre == UNDEFINED) {
            // op has no precondition on var --> it must start in v1 and v2.
            if (post == UNDEFINED) {
                // op has no effect on var --> it must end in v1 and v2.
                add_loop(v_id, op_id);
            } else if (v2.contains(var, post)) {
                // op must end in v2.
                add_transition(v_id, op_id, v2_id);
            } else {
                // op must end in v1.
                assert(v1.contains(var, post));
                add_transition(v_id, op_id, v1_id);
            }
        } else if (v1.contains(var, pre)) {
            // op must start in v1.
            assert(post != UNDEFINED);
            if (v1.contains(var, post)) {
                // op must end in v1.
                add_loop(v1_id, op_id);
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                add_transition(v1_id, op_id, v2_id);
            }
        } else {
            // op must start in v2.
            assert(v2.contains(var, pre));
            assert(post != UNDEFINED);
            if (v1.contains(var, post)) {
                // op must end in v1.
                add_transition(v2_id, op_id, v1_id);
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                add_loop(v2_id, op_id);
            }
        }
    }
}

void MatchTree::rewire(
    const AbstractStates &states, const AbstractState &v,
    const AbstractState &v1, const AbstractState &v2, int var) {
    NodeID v_node_id = v.get_node_id();
    NodeID node_id = 0;
    while (node_id != v_node_id) {
        Node node = refinement_hierarchy.nodes[node_id];
        int node_var = node.get_var();
        int node_value = node.value;
        NodeID v_ancestor_id = node.left_child;
        NodeID other_node_id = node.right_child;
        if (v.contains(node_var, node_value)) {
            swap(v_ancestor_id, other_node_id);
        }

        if (debug) {
            cout << "node: " << node_id << ", a: " << v_ancestor_id
                 << ", b: " << other_node_id << endl;
        }

        Transitions &out = outgoing[node_id];
        for (auto it = out.begin(); it != out.end();) {
            Transition &t = *it;
            // TODO: simplify this and compute lazily.
            int post = get_postcondition_value(t.op_id, var);
            int pre = get_precondition_value(t.op_id, var);
            if (post == UNDEFINED) {
                ++it;
            } else if (pre == UNDEFINED) {
                ++it;
            } else {
                // TODO: use swap and pop or fill separate vector.
                it = out.erase(it);
                remove_transition(incoming[t.target_id], t.op_id, node_id);
                --num_non_loops;
                add_transition(v_ancestor_id, t.op_id, t.target_id);
                add_transition(other_node_id, t.op_id, t.target_id);
            }
        }

        Transitions &in = incoming[node_id];
        for (auto it = in.begin(); it != in.end();) {
            Transition &t = *it;
            NodeID u_id = t.target_id;
            int post = get_postcondition_value(t.op_id, var);
            if (post == UNDEFINED) {
                ++it;
            } else {
                // TODO: use swap and pop or fill separate vector.
                it = in.erase(it);
                remove_transition(outgoing[u_id], t.op_id, node_id);
                --num_non_loops;
                add_transition(u_id, t.op_id, v_ancestor_id);
                add_transition(u_id, t.op_id, other_node_id);
            }
        }

        for (auto it = loops[node_id].begin(); it != loops[node_id].end();) {
            int op_id = *it;
            int post = get_postcondition_value(op_id, var);
            if (post == UNDEFINED) {
                ++it;
            } else {
                // TODO: use swap and pop or fill separate vector.
                it = loops[node_id].erase(it);
                add_loop(v_ancestor_id, op_id);
                add_loop(other_node_id, op_id);
            }
        }

        node_id = v_ancestor_id;
    }

    enlarge_vectors_by_one();
    enlarge_vectors_by_one();

    rewire_incoming_transitions(states, v_node_id, v1, v2, var);
    rewire_outgoing_transitions(states, v_node_id, v1, v2, var);
    rewire_loops(v_node_id, v1, v2, var);
}

const vector<Transitions> &MatchTree::get_incoming_transitions() const {
    return incoming;
}

const vector<Transitions> &MatchTree::get_outgoing_transitions() const {
    return outgoing;
}

const vector<Loops> &MatchTree::get_loops() const {
    return loops;
}

int MatchTree::get_num_states() const {
    assert(incoming.size() == outgoing.size());
    assert(loops.size() == outgoing.size());
    return outgoing.size();
}

int MatchTree::get_num_operators() const {
    return preconditions_by_operator.size();
}

int MatchTree::get_num_non_loops() const {
    return num_non_loops;
}

int MatchTree::get_num_loops() const {
    return num_loops;
}

void MatchTree::print_statistics() const {
    int total_incoming_transitions = 0;
    int total_outgoing_transitions = 0;
    int total_loops = 0;
    for (int state_id = 0; state_id < get_num_states(); ++state_id) {
        total_incoming_transitions += incoming[state_id].size();
        total_outgoing_transitions += outgoing[state_id].size();
        total_loops += loops[state_id].size();
    }
    assert(total_outgoing_transitions == total_incoming_transitions);
    assert(get_num_loops() == total_loops);
    assert(get_num_non_loops() == total_outgoing_transitions);
    cout << "Looping transitions: " << total_loops << endl;
    cout << "Non-looping transitions: " << total_outgoing_transitions << endl;
}

void MatchTree::dump() const {
    for (int i = 0; i < get_num_states(); ++i) {
        cout << "Node " << i << endl;
        cout << "  ID: " << refinement_hierarchy.nodes[i].get_state_id() << endl;
        cout << "  in: " << incoming[i] << endl;
        cout << "  out: " << outgoing[i] << endl;
        cout << "  loops: " << loops[i] << endl;
    }
}
}
