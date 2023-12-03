#include "match_tree.h"

#include "utils.h"

#include "../operator_id.h"
#include "../task_proxy.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../tasks/inverted_task.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <algorithm>
#include <execution>
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

#ifndef NDEBUG
static bool contains_all_facts(const CartesianSet &set, const vector<FactPair> &facts) {
    return all_of(facts.begin(), facts.end(),
                  [&](const FactPair &fact) {return set.test(fact.var, fact.value);});
}
#endif

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

#ifndef NDEBUG
    sort(operators.begin(), operators.end());
    assert(utils::is_sorted_unique(operators));
#endif
    return operators;
}

Operators MatchTree::get_outgoing_operators(const AbstractState &state) const {
    Operators operators;
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

#ifndef NDEBUG
    sort(operators.begin(), operators.end());
    assert(utils::is_sorted_unique(operators));
#endif
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
    const CartesianSets &cartesian_sets,
    const AbstractState &state,
    const vector<int> &incoming_operators) const {
    Transitions transitions;
    for (int op_id : incoming_operators) {
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

Transitions MatchTree::get_incoming_transitions(
    const CartesianSets &cartesian_sets, const AbstractState &state) const {
    return get_incoming_transitions(cartesian_sets, state, get_incoming_operators(state));
}

Transitions MatchTree::get_outgoing_transitions(
    const CartesianSets &cartesian_sets,
    const AbstractState &state,
    const vector<int> &outgoing_operators) const {
    Transitions transitions;
    for (int op_id : outgoing_operators) {
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

Transitions MatchTree::get_outgoing_transitions(
    const CartesianSets &cartesian_sets, const AbstractState &state) const {
    return get_outgoing_transitions(cartesian_sets, state, get_outgoing_operators(state));
}

bool MatchTree::is_applicable(const AbstractState &src, int op_id) const {
    return all_of(preconditions[op_id].begin(), preconditions[op_id].end(),
                  [&src](const FactPair &pre) {
                      return src.contains(pre.var, pre.value);
                  });
}

bool MatchTree::has_transition(
    const AbstractState &src, int op_id, const AbstractState &dest,
    const vector<bool> *domains_intersect) const {
    assert(is_applicable(src, op_id));
    // Simultaneously loop over variables and postconditions.
    int num_vars = src.get_cartesian_set().get_num_variables();
    auto it = postconditions[op_id].begin();
    for (int var = 0; var < num_vars; ++var) {
        if (it != postconditions[op_id].end() && it->var == var) {
            if (!dest.contains(var, it->value)) {
                return false;
            }
            ++it;
        } else if (
            (domains_intersect && !(*domains_intersect)[var]) ||
            (!domains_intersect && !src.domain_subsets_intersect(dest.get_cartesian_set(), var))) {
            return false;
        }
    }
    return true;
}

bool MatchTree::has_transition(
    const AbstractState &src, int op_id, const AbstractState &dest) const {
    if (!is_applicable(src, op_id)) {
        return false;
    }
    return has_transition(src, op_id, dest, nullptr);
}

int MatchTree::get_operator_between_states(
    const AbstractState &src, const AbstractState &dest, int cost) const {
    int num_vars = src.get_cartesian_set().get_num_variables();
    vector<bool> domains_intersect(num_vars, false);
    for (int var = 0; var < num_vars; ++var) {
        domains_intersect[var] = src.domain_subsets_intersect(dest.get_cartesian_set(), var);
    }
    vector<int> operators = get_outgoing_operators(src);
    if (g_hacked_sort_transitions) {
        sort(execution::unseq, operators.begin(), operators.end());
    }
    for (int op_id : operators) {
        if (operator_costs[op_id] == cost && has_transition(src, op_id, dest, &domains_intersect)) {
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

int MatchTree::get_num_operators() const {
    return preconditions.size();
}

void MatchTree::print_statistics() const {
    uint64_t static_mem_usage = 0;
    static_mem_usage += estimate_memory_usage_in_bytes(preconditions);
    static_mem_usage += estimate_memory_usage_in_bytes(effects);
    static_mem_usage += estimate_memory_usage_in_bytes(postconditions);
    static_mem_usage += estimate_memory_usage_in_bytes(effect_vars_without_preconditions);
    cout << "Match tree estimated memory usage for operator info: "
         << static_mem_usage / 1024 << " KB" << endl;
    basic_string<char> test;
}
}
