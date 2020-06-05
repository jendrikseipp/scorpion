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

static vector<vector<FactPair>> get_effects_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> effects_by_operator;
    effects_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        vector<FactPair> effects;
        effects.reserve(op.get_effects().size());
        for (EffectProxy effect : op.get_effects()) {
            effects.push_back(effect.get_fact().get_pair());
        }
        sort(effects.begin(), effects.end());
        effects_by_operator.push_back(move(effects));
    }
    return effects_by_operator;
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

static vector<int> get_operator_costs(const OperatorsProxy &operators) {
    vector<int> costs;
    costs.reserve(operators.size());
    for (OperatorProxy op : operators)
        costs.push_back(op.get_cost());
    return costs;
}


MatchTree::MatchTree(
    const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
    const CartesianSets &cartesian_sets, bool debug)
    : preconditions(get_preconditions_by_operator(ops)),
      effects(get_effects_by_operator(ops)),
      postconditions(get_postconditions_by_operator(ops)),
      operator_costs(get_operator_costs(ops)),
      refinement_hierarchy(refinement_hierarchy),
      cartesian_sets(cartesian_sets),
      debug(debug) {
    add_operators_in_trivial_abstraction();
}

int MatchTree::get_precondition_value(int op_id, int var) const {
    return lookup_value(preconditions[op_id], var);
}

int MatchTree::get_postcondition_value(int op_id, int var) const {
    return lookup_value(postconditions[op_id], var);
}

int MatchTree::get_state_id(NodeID node_id) const {
    return refinement_hierarchy.get_abstract_state_id(node_id);
}

void MatchTree::resize_vectors(int new_size) {
    outgoing.resize(new_size);
    incoming.resize(new_size);
}

void MatchTree::add_operators_in_trivial_abstraction() {
    assert(get_num_nodes() == 0);
    resize_vectors(1);
    int init_id = 0;
    incoming[init_id].reserve(get_num_operators());
    outgoing[init_id].reserve(get_num_operators());
    for (int i = 0; i < get_num_operators(); ++i) {
        incoming[init_id].push_back(i);
        outgoing[init_id].push_back(i);
    }
}

static bool contains_all_facts(const CartesianSet &set, const vector<FactPair> &facts) {
    return all_of(facts.begin(), facts.end(),
                  [&](const FactPair &fact) {return set.test(fact.var, fact.value);});
}

void MatchTree::split(
    const CartesianSets &cartesian_sets, const AbstractState &v, int var) {
    int new_num_nodes = cartesian_sets.size();
    resize_vectors(new_num_nodes);
    assert(get_num_nodes() == new_num_nodes);

    refinement_hierarchy.for_each_visited_family(
        v, [&](NodeID node_id, Children children) {
            NodeID v_ancestor_id = children.correct_child;
            NodeID other_node_id = children.other_child;

            Operators old_outgoing;
            swap(old_outgoing, outgoing[node_id]);
            for (int op_id : old_outgoing) {
                int pre = get_precondition_value(op_id, var);
                if (pre == UNDEFINED) {
                    outgoing[node_id].push_back(op_id);
                } else {
                    // TODO: at least one of the children must get the operator.
                    if (cartesian_sets[v_ancestor_id]->test(var, pre)) {
                        assert(contains_all_facts(*cartesian_sets[v_ancestor_id],
                                                  preconditions[op_id]));
                        outgoing[v_ancestor_id].push_back(op_id);
                    }
                    if (cartesian_sets[other_node_id]->test(var, pre)) {
                        assert(contains_all_facts(*cartesian_sets[other_node_id],
                                                  preconditions[op_id]));
                        outgoing[other_node_id].push_back(op_id);
                    }
                }
            }

            Operators old_incoming;
            swap(old_incoming, incoming[node_id]);
            for (int op_id : old_incoming) {
                int post = get_postcondition_value(op_id, var);
                if (post == UNDEFINED) {
                    incoming[node_id].push_back(op_id);
                } else {
                    // TODO: at least one of the children must get the operator.
                    if (cartesian_sets[v_ancestor_id]->test(var, post)) {
                        assert(contains_all_facts(*cartesian_sets[v_ancestor_id],
                                                  postconditions[op_id]));
                        incoming[v_ancestor_id].push_back(op_id);
                    }
                    if (cartesian_sets[other_node_id]->test(var, post)) {
                        assert(contains_all_facts(*cartesian_sets[other_node_id],
                                                  postconditions[op_id]));
                        incoming[other_node_id].push_back(op_id);
                    }
                }
            }

            for (NodeID id : {node_id, v_ancestor_id, other_node_id}) {
                incoming[id].shrink_to_fit();
                outgoing[id].shrink_to_fit();
            }
        });
}

Operators MatchTree::get_incoming_operators(const AbstractState &state) const {
    Operators operators;
    refinement_hierarchy.for_each_visited_node(
        state, [&](const NodeID &node_id) {
            assert(cartesian_sets[node_id]->is_superset_of(state.get_cartesian_set()));
            operators.reserve(operators.size() + incoming[node_id].size());
            for (int op_id : incoming[node_id]) {
                assert(contains_all_facts(state.get_cartesian_set(),
                                          postconditions[op_id]));
                // Ignore operators with infinite cost.
                if (operator_costs[op_id] != INF) {
                    operators.push_back(op_id);
                }
            }
        });
    return operators;
}

Operators MatchTree::get_outgoing_operators(const AbstractState &state) const {
    Operators operators;
    refinement_hierarchy.for_each_visited_node(
        state, [&](const NodeID &node_id) {
            assert(cartesian_sets[node_id]->is_superset_of(state.get_cartesian_set()));
            operators.reserve(operators.size() + outgoing[node_id].size());
            for (int op_id : outgoing[node_id]) {
                assert(contains_all_facts(state.get_cartesian_set(),
                                          preconditions[op_id]));
                // Filter self-loops. An operator loops iff state contains all its effects,
                // since then the resulting Cartesian set is a subset of state.
                // TODO: ignore operators with infinite cost.
                if (any_of(effects[op_id].begin(), effects[op_id].end(),
                           [&state](const FactPair &fact) {return !state.contains(fact.var, fact.value);})) {
                    operators.push_back(op_id);
                }
            }
        });
    return operators;
}

Transitions MatchTree::get_incoming_transitions(
    const CartesianSets &cartesian_sets, const AbstractState &state) const {
    Transitions transitions;
    for (int op_id : get_incoming_operators(state)) {
        CartesianSet tmp_cartesian_set = state.get_cartesian_set();
        for (const FactPair &fact : effects[op_id]) {
            tmp_cartesian_set.add_all(fact.var);
        }
        for (const FactPair &fact : preconditions[op_id]) {
            tmp_cartesian_set.set_single_value(fact.var, fact.value);
        }
        refinement_hierarchy.for_each_leaf(
            cartesian_sets, tmp_cartesian_set, [&](NodeID leaf_id) {
                int src_state_id = get_state_id(leaf_id);
                // Filter self-loops.
                // TODO: can we filter self-loops earlier?
                if (src_state_id != state.get_id()) {
                    transitions.emplace_back(op_id, src_state_id);
                }
            });
    }
    return transitions;
}

Transitions MatchTree::get_outgoing_transitions(
    const CartesianSets &cartesian_sets, const AbstractState &state) const {
    Transitions transitions;
    for (int op_id : get_outgoing_operators(state)) {
        CartesianSet tmp_cartesian_set = state.get_cartesian_set();
        for (const FactPair &fact : postconditions[op_id]) {
            tmp_cartesian_set.set_single_value(fact.var, fact.value);
        }
        refinement_hierarchy.for_each_leaf(
            cartesian_sets, tmp_cartesian_set, [&](NodeID leaf_id) {
                int dest_state_id = get_state_id(leaf_id);
                assert(dest_state_id != state.get_id());
                transitions.emplace_back(op_id, dest_state_id);
            });
    }
    return transitions;
}

bool MatchTree::has_transition(const AbstractState &src, int op_id, const AbstractState &dest) const {
    int num_vars = src.get_cartesian_set().get_num_variables();
    vector<int> values(num_vars, -1);
    for (const FactPair &fact : postconditions[op_id]) {
        values[fact.var] = fact.value;
    }
    for (int var = 0; var < num_vars; ++var) {
        int value = values[var];
        if ((value == -1 && !src.domain_subsets_intersect(dest.get_cartesian_set(), var)) ||
            (value != -1 && !dest.contains(var, value))) {
            return false;
        }
    }
    return true;
}

int MatchTree::get_operator_between_states(
    const AbstractState &src, const AbstractState &dest, int cost) const {
    for (int op_id : get_outgoing_operators(src)) {
        if (operator_costs[op_id] == cost && has_transition(src, op_id, dest)) {
            return op_id;
        }
    }
    return UNDEFINED;
}

int MatchTree::get_num_nodes() const {
    assert(incoming.size() == outgoing.size());
    return outgoing.size();
}

int MatchTree::get_num_operators() const {
    return preconditions.size();
}

void MatchTree::print_statistics() const {
    int total_incoming_ops = 0;
    int total_outgoing_ops = 0;
    int total_capacity = 0;
    for (int node_id = 0; node_id < get_num_nodes(); ++node_id) {
        total_incoming_ops += incoming[node_id].size();
        total_outgoing_ops += outgoing[node_id].size();
        total_capacity += incoming[node_id].capacity() + outgoing[node_id].capacity();
    }
    cout << "Match tree incoming operators: " << total_incoming_ops << endl;
    cout << "Match tree outgoing operators: " << total_outgoing_ops << endl;
    cout << "Match tree capacity: " << total_capacity << endl;
    uint64_t mem_usage = 0;
    mem_usage += estimate_vector_of_vector_bytes(incoming);
    mem_usage += estimate_vector_of_vector_bytes(outgoing);
    cout << "Match tree estimated memory usage: " << mem_usage / 1024 << " KB" << endl;
    uint64_t static_mem_usage = 0;
    static_mem_usage += estimate_memory_usage_in_bytes(preconditions);
    static_mem_usage += estimate_memory_usage_in_bytes(effects);
    static_mem_usage += estimate_memory_usage_in_bytes(postconditions);
    cout << "Match tree estimated memory usage for operator info: "
         << static_mem_usage / 1024 << " KB" << endl;
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
