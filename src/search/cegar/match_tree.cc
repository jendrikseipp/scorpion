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


MatchTree::MatchTree(
    const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
    bool debug)
    : preconditions_by_operator(get_preconditions_by_operator(ops)),
      postconditions_by_operator(get_postconditions_by_operator(ops)),
      refinement_hierarchy(refinement_hierarchy),
      debug(debug) {
    add_operators_in_trivial_abstraction();
}

int MatchTree::get_precondition_value(int op_id, int var) const {
    return lookup_value(preconditions_by_operator[op_id], var);
}

int MatchTree::get_postcondition_value(int op_id, int var) const {
    return lookup_value(postconditions_by_operator[op_id], var);
}

int MatchTree::get_state_id(NodeID node_id) const {
    return refinement_hierarchy.nodes[node_id].get_state_id();
}

void MatchTree::enlarge_vectors_by_one() {
    int new_num_nodes = get_num_nodes() + 1;
    outgoing.resize(new_num_nodes);
    incoming.resize(new_num_nodes);
}

void MatchTree::add_operators_in_trivial_abstraction() {
    assert(get_num_nodes() == 0);
    enlarge_vectors_by_one();
    int init_id = 0;
    incoming[init_id].reserve(get_num_operators());
    outgoing[init_id].reserve(get_num_operators());
    for (int i = 0; i < get_num_operators(); ++i) {
        incoming[init_id].push_back(i);
        outgoing[init_id].push_back(i);
    }
}

void MatchTree::split(
    const CartesianSets &cartesian_sets, const AbstractState &v, int var) {
    enlarge_vectors_by_one();
    enlarge_vectors_by_one();
    refinement_hierarchy.for_each_visited_family(
        v, [&](const Family &family) {
            NodeID node_id = family.parent;
            NodeID v_ancestor_id = family.correct_child;
            NodeID other_node_id = family.other_child;

            Operators &out = outgoing[node_id];
            for (auto it = out.begin(); it != out.end();) {
                int op_id = *it;
                // TODO: simplify this and compute lazily.
                int post = get_postcondition_value(op_id, var);
                int pre = get_precondition_value(op_id, var);
                if (post == UNDEFINED) {
                    ++it;
                } else if (pre == UNDEFINED) {
                    ++it;
                } else {
                    // TODO: use swap and pop or fill separate vector.
                    it = out.erase(it);
                    if (cartesian_sets[v_ancestor_id]->test(var, pre)) {
                        outgoing[v_ancestor_id].push_back(op_id);
                    }
                    if (cartesian_sets[other_node_id]->test(var, pre)) {
                        outgoing[other_node_id].push_back(op_id);
                    }
                }
            }

            Operators &in = incoming[node_id];
            for (auto it = in.begin(); it != in.end();) {
                int op_id = *it;
                int post = get_postcondition_value(op_id, var);
                if (post == UNDEFINED) {
                    ++it;
                } else {
                    // TODO: use swap and pop or fill separate vector.
                    it = in.erase(it);
                    if (cartesian_sets[v_ancestor_id]->test(var, post)) {
                        incoming[v_ancestor_id].push_back(op_id);
                    }
                    if (cartesian_sets[other_node_id]->test(var, post)) {
                        incoming[other_node_id].push_back(op_id);
                    }
                }
            }

            node_id = v_ancestor_id;
        });
}

Operators MatchTree::get_incoming_operators(const AbstractState &state) const {
    Operators operators;
    refinement_hierarchy.for_each_visited_node(
        state, [&](const NodeID &node_id) {
            // TODO: append whole vector at once.
            for (int op_id : incoming[node_id]) {
                operators.push_back(op_id);
            }
        });
    return operators;
}

Operators MatchTree::get_outgoing_operators(const AbstractState &state) const {
    Operators operators;
    refinement_hierarchy.for_each_visited_node(
        state, [&](const NodeID &node_id) {
            // TODO: append whole vector at once.
            for (int op_id : outgoing[node_id]) {
                operators.push_back(op_id);
            }
        });
    return operators;
}

Transitions MatchTree::get_outgoing_transitions(
    const CartesianSets &cartesian_sets, const AbstractState &state) const {
    Transitions transitions;
    Operators ops = get_outgoing_operators(state);
    for (int op_id : ops) {
        CartesianSet dest = state.get_cartesian_set();
        // Check that the operator is applicable.
        cout << "Cartesian set " << dest << ", pre: " << preconditions_by_operator[op_id] << endl;
        assert(all_of(preconditions_by_operator[op_id].begin(), preconditions_by_operator[op_id].end(),
                      [&](const FactPair &fact) {return state.contains(fact.var, fact.value);}));
        for (const FactPair &fact : postconditions_by_operator[op_id]) {
            dest.set_single_value(fact.var, fact.value);
        }
        cout << "  apply " << op_id << " in " << state << " -> " << dest << endl;
        refinement_hierarchy.for_each_leaf(
            cartesian_sets, dest, [&](NodeID leaf_id) {
                int dest_state_id = get_state_id(leaf_id);
                // Filter self-loops.
                // TODO: can we filter self-loops earlier?
                if (dest_state_id != state.get_id()) {
                    transitions.emplace_back(op_id, dest_state_id);
                }
            });
    }
    return transitions;
}

int MatchTree::get_num_nodes() const {
    assert(incoming.size() == outgoing.size());
    return outgoing.size();
}

int MatchTree::get_num_operators() const {
    return preconditions_by_operator.size();
}

void MatchTree::print_statistics() const {
    int total_incoming_ops = 0;
    int total_outgoing_ops = 0;
    for (int node_id = 0; node_id < get_num_nodes(); ++node_id) {
        total_incoming_ops += incoming[node_id].size();
        total_outgoing_ops += outgoing[node_id].size();
    }
    cout << "Incoming operators: " << total_incoming_ops << endl;
    cout << "Outgoing operators: " << total_outgoing_ops << endl;
}

void MatchTree::dump() const {
    for (int i = 0; i < get_num_nodes(); ++i) {
        cout << "Node " << i << endl;
        cout << "  ID: " << get_state_id(i) << endl;
        cout << "  in: " << incoming[i] << endl;
        cout << "  out: " << outgoing[i] << endl;
    }
}
}
