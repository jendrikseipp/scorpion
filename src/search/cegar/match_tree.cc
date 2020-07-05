#include "match_tree.h"

#include "utils.h"

#include "../operator_id.h"
#include "../task_proxy.h"

#include "../heuristics/additive_heuristic.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../tasks/cost_adapted_task.h"
#include "../tasks/inverted_task.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

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

static vector<int> get_effect_vars_without_preconditions(
    const OperatorProxy &op) {
    unordered_set<int> vars_with_precondition;
    for (FactProxy fact : op.get_preconditions()) {
        vars_with_precondition.insert(fact.get_variable().get_id());
    }
    vector<int> vars;
    for (EffectProxy effect : op.get_effects()) {
        int var = effect.get_fact().get_variable().get_id();
        if (!vars_with_precondition.count(var)) {
            vars.push_back(var);
        }
    }
    sort(vars.begin(), vars.end());
    vars.shrink_to_fit();
    return vars;
}

static vector<vector<int>> get_effect_vars_without_preconditions_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<int>> result;
    result.reserve(ops.size());
    for (OperatorProxy op : ops) {
        result.push_back(get_effect_vars_without_preconditions(op));
    }
    return result;
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

static bool operator_applicable(
    const OperatorProxy &op, const utils::HashSet<FactProxy> &facts) {
    for (FactProxy precondition : op.get_preconditions()) {
        if (!facts.count(precondition))
            return false;
    }
    return true;
}

static vector<int> compute_relaxed_plan_layer_per_operator(
    const TaskProxy &task_proxy) {
    int unreachable = INF;
    vector<int> layers(task_proxy.get_operators().size(), unreachable);
    utils::HashSet<FactProxy> reached_facts;

    // Add facts from initial state.
    for (FactProxy fact : task_proxy.get_initial_state()) {
        reached_facts.insert(fact);
    }

    /*
      Note: This can be done more efficiently by maintaining the number
      of unsatisfied preconditions for each operator and a queue of
      unhandled effects.

      TODO: Find out if this code is time critical, and change it if it is.
    */
    int layer = 0;
    bool new_ops_applicable = true;
    while (new_ops_applicable) {
        new_ops_applicable = false;
        utils::HashSet<FactProxy> new_reached_facts;
        for (OperatorProxy op : task_proxy.get_operators()) {
            // Add all facts that are achieved by an applicable operator.
            if (layers[op.get_id()] == unreachable && operator_applicable(op, reached_facts)) {
                layers[op.get_id()] = layer;
                new_ops_applicable = true;
                for (EffectProxy effect : op.get_effects()) {
                    new_reached_facts.insert(effect.get_fact());
                }
            }
        }
        for (auto &fact : new_reached_facts) {
            reached_facts.insert(fact);
        }
        ++layer;
    }
    if (any_of(layers.begin(), layers.end(), [&](int l) {return l == unreachable;})) {
        cerr << ("Warning: task contains a relaxed unreachable operator.") << endl;
    }
    return layers;
}

static void print_count(const vector<int> &vec) {
    map<int, int> count;
    for (int layer : vec) {
        ++count[layer];
    }
    cout << "{";
    string sep = "";
    for (auto &pair : count) {
        cout << sep << pair.first << ":" << pair.second;
        sep = ", ";
    }
    cout << "}" << endl;
}

static vector<int> compute_relaxed_task_operator_costs(
    const string &name, const shared_ptr<AbstractTask> &task) {
    auto hadd = create_additive_heuristic(task);
    hadd->compute_heuristic_for_cegar(TaskProxy(*task).get_initial_state());

    int num_ops = TaskProxy(*task).get_operators().size();
    vector<int> relaxed_task_costs(num_ops, UNDEFINED);
    for (auto &unary_op : hadd->get_unary_operators_for_cegar()) {
        int &cost = relaxed_task_costs[unary_op.operator_no];
        if (cost == UNDEFINED) {
            cost = unary_op.cost;
        } else if (cost != unary_op.cost) {
            ABORT("Costs for relaxed unary operators differ");
        }
    }
    assert(!count(relaxed_task_costs.begin(), relaxed_task_costs.end(), UNDEFINED));
    cout << "Relaxed task operator " << name << ": ";
    print_count(relaxed_task_costs);
    return relaxed_task_costs;
}


MatchTree::MatchTree(
    const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
    const CartesianSets &cartesian_sets, bool debug)
    : num_variables(refinement_hierarchy.get_task_proxy().get_variables().size()),
      preconditions(get_preconditions_by_operator(ops)),
      effects(get_effects_by_operator(ops)),
      postconditions(get_postconditions_by_operator(ops)),
      effect_vars_without_preconditions(get_effect_vars_without_preconditions_by_operator(ops)),
      operator_costs(get_operator_costs(ops)),
      refinement_hierarchy(refinement_hierarchy),
      cartesian_sets(cartesian_sets),
      inverted_task(make_shared<extra_tasks::InvertedTask>(refinement_hierarchy.get_task())),
      forward_successor_generator(
          successor_generator::g_successor_generators[refinement_hierarchy.get_task_proxy()]),
      backward_successor_generator(
          successor_generator::g_successor_generators[TaskProxy(*inverted_task)]),
      sort_applicable_operators_by_increasing_cost(
          !task_properties::is_unit_cost(refinement_hierarchy.get_task_proxy())),
      debug(debug) {
    utils::Timer layer_timer;
    relaxed_task_layer = compute_relaxed_plan_layer_per_operator(refinement_hierarchy.get_task_proxy());
    utils::g_log << "Time for computing relaxed task operator layers: " << layer_timer << endl;
    cout << "Relaxed task operator layers: ";
    print_count(relaxed_task_layer);

    if (g_hacked_operator_ordering == OperatorOrdering::FIXED ||
        g_hacked_operator_tiebreak == OperatorOrdering::FIXED) {
        fixed_operator_order.resize(get_num_operators());
        iota(fixed_operator_order.begin(), fixed_operator_order.end(), 0);
        g_hacked_rng->shuffle(fixed_operator_order);
    }

    add_operators_in_trivial_abstraction();

    relaxed_task_costs = compute_relaxed_task_operator_costs(
        "costs", refinement_hierarchy.get_task());
    relaxed_task_steps = compute_relaxed_task_operator_costs(
        "steps", make_shared<tasks::CostAdaptedTask>(refinement_hierarchy.get_task(), OperatorCost::ONE));

    if (false) {
        for (int op = 0; op < get_num_operators(); ++op) {
            cout << "op " << op << " "
                 << refinement_hierarchy.get_task_proxy().get_operators()[op].get_name()
                 << ": " << relaxed_task_layer[op] << " "
                 << relaxed_task_costs[op] << endl;
        }
    }
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
    if (g_hacked_tsr == TransitionRepresentation::MT) {
        incoming.resize(new_size);
        outgoing.resize(new_size);
    }
}

void MatchTree::add_operators_in_trivial_abstraction() {
    assert(get_num_nodes() == 0);
    resize_vectors(1);
    if (g_hacked_tsr == TransitionRepresentation::MT) {
        int init_id = 0;
        incoming[init_id].reserve(get_num_operators());
        outgoing[init_id].reserve(get_num_operators());
        for (int i = 0; i < get_num_operators(); ++i) {
            incoming[init_id].push_back(i);
            outgoing[init_id].push_back(i);
        }
    }
}

static bool contains_all_facts(const CartesianSet &set, const vector<FactPair> &facts) {
    return all_of(facts.begin(), facts.end(),
                  [&](const FactPair &fact) {return set.test(fact.var, fact.value);});
}

void MatchTree::split(
    const CartesianSets &cartesian_sets, const AbstractState &v, int var) {
    if (g_hacked_tsr != TransitionRepresentation::MT) {
        return;
    }
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

bool MatchTree::incoming_operator_only_loops(const AbstractState &state, int op_id) const {
    for (const FactPair &fact : preconditions[op_id]) {
        if (!state.contains(fact.var, fact.value)) {
            return false;
        }
    }
    for (int var : effect_vars_without_preconditions[op_id]) {
        if (!state.get_cartesian_set().has_full_domain(var)) {
            return false;
        }
    }
    return true;
}

Operators MatchTree::get_incoming_operators(const AbstractState &state) const {
    Operators operators;
    if (g_hacked_tsr == TransitionRepresentation::SG) {
        vector<OperatorID> operator_ids;
        backward_successor_generator.generate_applicable_ops(state, operator_ids);
        for (OperatorID op_id : operator_ids) {
            int op = op_id.get_index();
            assert(contains_all_facts(state.get_cartesian_set(), postconditions[op]));
            // Ignore operators with infinite cost and operators that only loop.
            if (operator_costs[op] != INF && !incoming_operator_only_loops(state, op)) {
                operators.push_back(op);
            }
        }
    } else {
        assert(g_hacked_tsr == TransitionRepresentation::MT);
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
    }
#ifndef NDEBUG
    sort(operators.begin(), operators.end());
    assert(utils::is_sorted_unique(operators));
#endif
    order_operators(operators);
    return operators;
}

Operators MatchTree::get_outgoing_operators(const AbstractState &state) const {
    Operators operators;
    if (g_hacked_tsr == TransitionRepresentation::SG) {
        vector<OperatorID> operator_ids;
        forward_successor_generator.generate_applicable_ops(state, operator_ids);
        for (OperatorID op_id : operator_ids) {
            int op = op_id.get_index();
            assert(contains_all_facts(state.get_cartesian_set(), preconditions[op]));
            /*
              Ignore operators with infinite cost and filter self-loops. An
              operator loops iff state contains all its effects, since then the
              resulting Cartesian set is a subset of state.
            */
            if (operator_costs[op] != INF &&
                any_of(effects[op].begin(), effects[op].end(),
                       [&state](const FactPair &fact) {return !state.contains(fact.var, fact.value);})) {
                operators.push_back(op);
            }
        }
    } else {
        assert(g_hacked_tsr == TransitionRepresentation::MT);
        refinement_hierarchy.for_each_visited_node(
            state, [&](const NodeID &node_id) {
                assert(cartesian_sets[node_id]->is_superset_of(state.get_cartesian_set()));
                operators.reserve(operators.size() + outgoing[node_id].size());
                for (int op_id : outgoing[node_id]) {
                    assert(contains_all_facts(state.get_cartesian_set(),
                                              preconditions[op_id]));
                    /*
                      Ignore operators with infinite cost and filter
                      self-loops. An operator loops iff state contains all its
                      effects, since then the resulting Cartesian set is a
                      subset of state.
                    */
                    if (operator_costs[op_id] != INF &&
                        any_of(effects[op_id].begin(), effects[op_id].end(),
                               [&state](const FactPair &fact) {return !state.contains(fact.var, fact.value);})) {
                        operators.push_back(op_id);
                    }
                }
            });
    }
#ifndef NDEBUG
    sort(operators.begin(), operators.end());
    assert(utils::is_sorted_unique(operators));
#endif
    order_operators(operators);
    return operators;
}

Matcher MatchTree::get_incoming_matcher(int op_id) const {
    Matcher matcher(num_variables, Variable::UNAFFECTED);
    for (int var : effect_vars_without_preconditions[op_id]) {
        matcher[var] = Variable::FULL_DOMAIN;
    }
    for (const FactPair &fact : preconditions[op_id]) {
        matcher[fact.var] = Variable::SINGLE_VALUE;
    }
    return matcher;
}

Matcher MatchTree::get_outgoing_matcher(int op_id) const {
    Matcher matcher(num_variables, Variable::UNAFFECTED);
    for (const FactPair &fact : postconditions[op_id]) {
        matcher[fact.var] = Variable::SINGLE_VALUE;
    }
    return matcher;
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
            cartesian_sets, tmp_cartesian_set, get_incoming_matcher(op_id),
            [&](NodeID leaf_id) {
                int src_state_id = get_state_id(leaf_id);
                // Filter self-loops.
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
            cartesian_sets, tmp_cartesian_set, get_outgoing_matcher(op_id),
            [&](NodeID leaf_id) {
                int dest_state_id = get_state_id(leaf_id);
                assert(dest_state_id != state.get_id());
                transitions.emplace_back(op_id, dest_state_id);
            });
    }
    return transitions;
}

bool MatchTree::has_transition(
    const AbstractState &src, int op_id, const AbstractState &dest,
    const vector<bool> &domains_intersect) const {
    // Simultaneously loop over variables and postconditions.
    int num_vars = src.get_cartesian_set().get_num_variables();
    auto it = postconditions[op_id].begin();
    for (int var = 0; var < num_vars; ++var) {
        if (it != postconditions[op_id].end() && it->var == var) {
            if (!dest.contains(var, it->value)) {
                return false;
            }
            ++it;
        } else if (!domains_intersect[var]) {
            return false;
        }
    }
    return true;
}

function<int(int)> MatchTree::get_order_key(OperatorOrdering ordering) const {
    function<int(int)> key;
    if (ordering == OperatorOrdering::FIXED) {
        key = [&](int op) {return fixed_operator_order[op];};
    } else if (ordering == OperatorOrdering::ID_UP) {
        key = [](int op) {return op;};
    } else if (ordering == OperatorOrdering::ID_DOWN) {
        key = [](int op) {return -op;};
    } else if (ordering == OperatorOrdering::COST_UP) {
        key = [&](int op) {return operator_costs[op];};
    } else if (ordering == OperatorOrdering::COST_DOWN) {
        key = [&](int op) {return -operator_costs[op];};
    } else if (ordering == OperatorOrdering::POSTCONDITIONS_UP) {
        key = [&](int op) {return postconditions[op].size();};
    } else if (ordering == OperatorOrdering::POSTCONDITIONS_DOWN) {
        key = [&](int op) {return -postconditions[op].size();};
    } else if (ordering == OperatorOrdering::LAYER_UP) {
        key = [&](int op) {return relaxed_task_layer[op];};
    } else if (ordering == OperatorOrdering::LAYER_DOWN) {
        key = [&](int op) {return -relaxed_task_layer[op];};
    } else if (ordering == OperatorOrdering::HADD_UP) {
        key = [&](int op) {return relaxed_task_costs[op];};
    } else if (ordering == OperatorOrdering::HADD_DOWN) {
        key = [&](int op) {return -relaxed_task_costs[op];};
    } else if (ordering == OperatorOrdering::STEPS_UP) {
        key = [&](int op) {return relaxed_task_steps[op];};
    } else if (ordering == OperatorOrdering::STEPS_DOWN) {
        key = [&](int op) {return -relaxed_task_steps[op];};
    } else {
        ABORT("Unknown operator ordering");
    }
    return key;
}

void MatchTree::order_operators(std::vector<int> &operators) const {
    g_hacked_rng->shuffle(operators);
    if (g_hacked_operator_ordering == OperatorOrdering::RANDOM) {
        return;
    }
    if (g_hacked_operator_tiebreak == OperatorOrdering::RANDOM) {
        ABORT("operator order tie-breaking can't be random");
    }
    std::function<int(int)> key1 = get_order_key(g_hacked_operator_ordering);
    std::function<int(int)> key2 = get_order_key(g_hacked_operator_tiebreak);
    sort(operators.begin(), operators.end(), [&](int op1, int op2) {
             return make_pair(key1(op1), key2(op1)) < make_pair(key1(op2), key2(op2));
         });
}

int MatchTree::get_operator_between_states(
    const AbstractState &src, const AbstractState &dest, int cost) const {
    int num_vars = src.get_cartesian_set().get_num_variables();
    vector<bool> domains_intersect(num_vars, false);
    for (int var = 0; var < num_vars; ++var) {
        domains_intersect[var] = src.domain_subsets_intersect(dest.get_cartesian_set(), var);
    }
    for (int op_id : get_outgoing_operators(src)) {
        if (operator_costs[op_id] == cost && has_transition(src, op_id, dest, domains_intersect)) {
            return op_id;
        }
    }
    return UNDEFINED;
}

vector<bool> MatchTree::get_looping_operators(const AbstractStates &states) const {
    vector<bool> looping(preconditions.size(), false);
    vector<OperatorID> applicable_ops;
    for (auto &state : states) {
        applicable_ops.clear();
        forward_successor_generator.generate_applicable_ops(*state, applicable_ops);
        for (OperatorID op_id : applicable_ops) {
            int op = op_id.get_index();
            if (looping[op]) {
                continue;
            }
            assert(contains_all_facts(state->get_cartesian_set(), preconditions[op]));
            // An operator loops iff state contains all its effects,
            // since then the resulting Cartesian set is a subset of state.
            // TODO: is it faster to compute the intersection of incoming and outgoing operators?
            if (all_of(effects[op].begin(), effects[op].end(),
                       [&state](const FactPair &fact) {
                           return state->contains(fact.var, fact.value);
                       })) {
                looping[op] = true;
            }
        }
    }
    return looping;
}

int MatchTree::get_num_nodes() const {
    assert(incoming.size() == outgoing.size());
    return outgoing.size();
}

int MatchTree::get_num_operators() const {
    return preconditions.size();
}

void MatchTree::print_statistics() const {
    if (g_hacked_tsr == TransitionRepresentation::MT) {
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
    }
    uint64_t static_mem_usage = 0;
    static_mem_usage += estimate_memory_usage_in_bytes(preconditions);
    static_mem_usage += estimate_memory_usage_in_bytes(effects);
    static_mem_usage += estimate_memory_usage_in_bytes(postconditions);
    static_mem_usage += estimate_memory_usage_in_bytes(effect_vars_without_preconditions);
    cout << "Match tree estimated memory usage for operator info: "
         << static_mem_usage / 1024 << " KB" << endl;
    if (debug) {
        dump();
    }
    basic_string<char> test;
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
