#include "match_tree.h"

#include "transition.h"
#include "utils.h"

#include "../operator_id.h"
#include "../task_proxy.h"

#include "../task_utils/successor_generator.h"
#include "../tasks/inverted_task.h"
#include "../utils/timer.h"

#include <algorithm>
#include <execution>

using namespace std;

namespace cartesian_abstractions {
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

static vector<int> get_operator_costs(const OperatorsProxy &operators) {
    vector<int> costs;
    costs.reserve(operators.size());
    for (OperatorProxy op : operators)
        costs.push_back(op.get_cost());
    return costs;
}


MatchTree::MatchTree(
    const OperatorsProxy &ops,
    const vector<Facts> &preconditions_by_operator,
    const vector<Facts> &postconditions_by_operator,
    const RefinementHierarchy &refinement_hierarchy,
    const CartesianSets &cartesian_sets,
    bool debug)
    : num_variables(refinement_hierarchy.get_task_proxy().get_variables().size()),
      preconditions(preconditions_by_operator),
      effects(get_effects_by_operator(ops)),
      postconditions(postconditions_by_operator),
      effect_vars_without_preconditions(get_effect_vars_without_preconditions_by_operator(ops)),
      operator_costs(get_operator_costs(ops)),
      refinement_hierarchy(refinement_hierarchy),
      cartesian_sets(cartesian_sets),
      inverted_task(make_shared<extra_tasks::InvertedTask>(refinement_hierarchy.get_task())),
      forward_successor_generator(
          successor_generator::g_successor_generators[refinement_hierarchy.get_task_proxy()]),
      backward_successor_generator(
          successor_generator::g_successor_generators[TaskProxy(*inverted_task)]),
      debug(debug) {
    utils::Timer layer_timer;
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
    Matcher matcher(num_variables, MatcherVariable::UNAFFECTED);
    for (int var : effect_vars_without_preconditions[op_id]) {
        matcher[var] = MatcherVariable::FULL_DOMAIN;
    }
    for (const FactPair &fact : preconditions[op_id]) {
        matcher[fact.var] = MatcherVariable::SINGLE_VALUE;
    }
    return matcher;
}

Matcher MatchTree::get_outgoing_matcher(int op_id) const {
    Matcher matcher(num_variables, MatcherVariable::UNAFFECTED);
    for (const FactPair &fact : postconditions[op_id]) {
        matcher[fact.var] = MatcherVariable::SINGLE_VALUE;
    }
    return matcher;
}

Transitions MatchTree::get_incoming_transitions(
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

Transitions MatchTree::get_incoming_transitions(const AbstractState &state) const {
    return get_incoming_transitions(state, get_incoming_operators(state));
}

Transitions MatchTree::get_outgoing_transitions(
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

Transitions MatchTree::get_outgoing_transitions(const AbstractState &state) const {
    return get_outgoing_transitions(state, get_outgoing_operators(state));
}

bool MatchTree::is_applicable(const AbstractState &src, int op_id) const {
#ifdef NDEBUG
    ABORT("MatchTree::is_applicable() should only be called in debug mode.");
#endif
    return all_of(preconditions[op_id].begin(), preconditions[op_id].end(),
                  [&src](const FactPair &pre) {
                      return src.contains(pre.var, pre.value);
                  });
}

bool MatchTree::has_transition(
    const AbstractState &src, int op_id, const AbstractState &dest) const {
#ifdef NDEBUG
    ABORT("MatchTree::has_transition() should only be called in debug mode.");
#endif
    if (!is_applicable(src, op_id)) {
        return false;
    }
    // Simultaneously loop over variables and postconditions.
    int num_vars = src.get_cartesian_set().get_num_variables();
    auto it = postconditions[op_id].begin();
    for (int var = 0; var < num_vars; ++var) {
        if (it != postconditions[op_id].end() && it->var == var) {
            if (!dest.contains(var, it->value)) {
                return false;
            }
            ++it;
        } else if (!src.domain_subsets_intersect(dest, var)) {
            return false;
        }
    }
    return true;
}

vector<bool> MatchTree::get_looping_operators(const AbstractStates &states) const {
    /* TODO: Is it faster to consider each op, use the refinement hierarchy to get
       the set of states op is applicable in and check whether it loops for one of them? */
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
}
}
