#include "pattern_evaluator.h"

#include "match_tree.h"

#include "../algorithms/partial_state_tree.h"
#include "../algorithms/priority_queues.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <cassert>
#include <unordered_map>

using namespace std;

namespace pdbs {
using PreconditionsTree = partial_state_tree::PartialStateTree;
using AbstractOperatorSet = utils::HashMap<vector<FactPair>, PreconditionsTree>;

static const int INF = numeric_limits<int>::max();

static int compute_hash_effect(
    const vector<FactPair> &preconditions,
    const vector<FactPair> &effects,
    const vector<int> &hash_multipliers,
    bool forward) {
    int hash_effect = 0;
    assert(preconditions.size() == effects.size());
    for (size_t i = 0; i < preconditions.size(); ++i) {
        int var = preconditions[i].var;
        assert(var == effects[i].var);
        int old_val = preconditions[i].value;
        int new_val = effects[i].value;
        assert(old_val != -1);
        if (!forward) {
            swap(old_val, new_val);
        }
        int effect = (new_val - old_val) * hash_multipliers[var];
        hash_effect += effect;
    }
    return hash_effect;
}

OperatorInfo::OperatorInfo(const OperatorProxy &op)
    : concrete_operator_id(op.get_id()) {
    preconditions.reserve(op.get_preconditions().size());
    for (FactProxy pre : op.get_preconditions()) {
        preconditions.push_back(pre.get_pair());
    }
    sort(preconditions.begin(), preconditions.end());

    effects.reserve(op.get_effects().size());
    for (EffectProxy eff : op.get_effects()) {
        effects.push_back(eff.get_fact().get_pair());
    }
    sort(effects.begin(), effects.end());
}

TaskInfo::TaskInfo(const TaskProxy &task_proxy) {
    num_variables = task_proxy.get_variables().size();
    for (VariableProxy var : task_proxy.get_variables()) {
        domain_sizes.push_back(var.get_domain_size());
    }
    goals = task_properties::get_fact_pairs(task_proxy.get_goals());
    int num_operators = task_proxy.get_operators().size();
    operator_infos.reserve(num_operators);
    for (OperatorProxy op : task_proxy.get_operators()) {
        operator_infos.emplace_back(op);
    }

    variable_effects.resize(num_operators * num_variables, false);
    for (const OperatorInfo &op : operator_infos) {
        for (const FactPair &effect : op.effects) {
            variable_effects[op.concrete_operator_id * num_variables + effect.var] = true;
        }
    }
}

int TaskInfo::get_num_operators() const {
    return operator_infos.size();
}

int TaskInfo::get_num_variables() const {
    return num_variables;
}


static bool operator_is_subsumed(
    const OperatorInfo &op,
    const vector<int> &variable_to_pattern_index,
    const vector<int> &pattern_domain_sizes,
    AbstractOperatorSet &seen_abstract_ops) {
    vector<FactPair> abstract_preconditions;
    for (const FactPair &pre : op.preconditions) {
        int pattern_var_id = variable_to_pattern_index[pre.var];
        if (pattern_var_id != -1) {
            abstract_preconditions.emplace_back(pattern_var_id, pre.value);
        }
    }

    vector<FactPair> abstract_effects;
    for (const FactPair &eff : op.effects) {
        int pattern_var_id = variable_to_pattern_index[eff.var];
        if (pattern_var_id != -1) {
            abstract_effects.emplace_back(pattern_var_id, eff.value);
        }
    }

    assert(is_sorted(abstract_preconditions.begin(), abstract_preconditions.end()));
    assert(is_sorted(abstract_effects.begin(), abstract_effects.end()));

    auto it = seen_abstract_ops.find(abstract_effects);
    if (it == seen_abstract_ops.end()) {
        seen_abstract_ops[move(abstract_effects)].add(
            abstract_preconditions, pattern_domain_sizes);
    } else {
        PreconditionsTree &preconditions_collection = it->second;
        if (preconditions_collection.subsumes(abstract_preconditions)) {
            return true;
        } else {
            preconditions_collection.add(
                abstract_preconditions, pattern_domain_sizes);
        }
    }
    return false;
}


PatternEvaluator::PatternEvaluator(
    const TaskProxy &task_proxy,
    const TaskInfo &task_info,
    const pdbs::Pattern &pattern,
    const vector<int> &costs)
    : task_info(task_info) {
    assert(utils::is_sorted_unique(pattern));

    vector<int> hash_multipliers;
    hash_multipliers.reserve(pattern.size());
    num_states = 1;
    for (int var : pattern) {
        hash_multipliers.push_back(num_states);
        if (utils::is_product_within_limit(
                num_states, task_info.domain_sizes[var], numeric_limits<int>::max())) {
            num_states *= task_info.domain_sizes[var];
        } else {
            cerr << "Given pattern is too large! (Overflow occured): " << endl;
            cerr << pattern << endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
    }

    vector<int> variable_to_pattern_index(task_info.get_num_variables(), -1);
    for (size_t i = 0; i < pattern.size(); ++i) {
        variable_to_pattern_index[pattern[i]] = i;
    }

    vector<int> pattern_domain_sizes;
    pattern_domain_sizes.reserve(pattern.size());
    for (int var : pattern) {
        pattern_domain_sizes.push_back(task_info.domain_sizes[var]);
    }

    match_tree_backward = utils::make_unique_ptr<pdbs::MatchTree>(
        task_proxy, pattern, hash_multipliers);

    vector<pair<int, int>> active_ops;
    for (const OperatorInfo &op : task_info.operator_infos) {
        if (costs[op.concrete_operator_id] != INF
            && task_info.operator_affects_pattern(pattern, op.concrete_operator_id)) {
            int num_preconditions = 0;
            for (const FactPair &pre : op.preconditions) {
                if (variable_to_pattern_index[pre.var] != -1) {
                    ++num_preconditions;
                }
            }
            active_ops.emplace_back(op.concrete_operator_id, num_preconditions);
        }
    }
    // Sort by increasing cost and precondition size.
    sort(active_ops.begin(), active_ops.end(),
         [&costs](pair<int, int> f1, pair<int, int> f2) {
             return make_pair(costs[f1.first], f1.second)
             < make_pair(costs[f2.first], f2.second);
         });

    AbstractOperatorSet seen_abstract_ops;
    for (pair<int, int> op_and_num_preconditions : active_ops) {
        const OperatorInfo &op = task_info.operator_infos[op_and_num_preconditions.first];
        if (!operator_is_subsumed(
                op, variable_to_pattern_index, pattern_domain_sizes, seen_abstract_ops)) {
            build_abstract_operators(
                hash_multipliers, op, variable_to_pattern_index, pattern_domain_sizes);
        }
    }
    abstract_backward_operators.shrink_to_fit();

    goal_states = compute_goal_states(
        hash_multipliers, pattern_domain_sizes, variable_to_pattern_index);
}

PatternEvaluator::~PatternEvaluator() {
}

vector<int> PatternEvaluator::compute_goal_states(
    const vector<int> &hash_multipliers,
    const vector<int> &pattern_domain_sizes,
    const vector<int> &variable_to_pattern_index) const {
    vector<int> goal_states;

    // Compute abstract goal var-val pairs.
    vector<FactPair> abstract_goals;
    for (const FactPair &goal : task_info.goals) {
        if (variable_to_pattern_index[goal.var] != -1) {
            abstract_goals.emplace_back(variable_to_pattern_index[goal.var], goal.value);
        }
    }

    for (int state_index = 0; state_index < num_states; ++state_index) {
        if (is_consistent(
                hash_multipliers, pattern_domain_sizes, state_index, abstract_goals)) {
            goal_states.push_back(state_index);
        }
    }

    return goal_states;
}

void PatternEvaluator::multiply_out(
    const vector<int> &hash_multipliers,
    int pos,
    int conc_op_id,
    vector<FactPair> &prevails,
    vector<FactPair> &preconditions,
    vector<FactPair> &effects,
    const vector<FactPair> &effects_without_pre,
    const vector<int> &pattern_domain_sizes) {
    if (pos == static_cast<int>(effects_without_pre.size())) {
        // All effects without precondition have been checked: insert op.
        if (!effects.empty()) {
            int abs_op_id = abstract_backward_operators.size();
            abstract_backward_operators.emplace_back(
                conc_op_id,
                compute_hash_effect(preconditions, effects, hash_multipliers, false));
            vector<FactPair> regression_preconditions = prevails;
            regression_preconditions.insert(
                regression_preconditions.end(), effects.begin(), effects.end());
            sort(regression_preconditions.begin(), regression_preconditions.end());
            match_tree_backward->insert(abs_op_id, regression_preconditions);
        }
    } else {
        // For each possible value for the current variable, build an
        // abstract operator.
        int var_id = effects_without_pre[pos].var;
        int eff = effects_without_pre[pos].value;
        for (int i = 0; i < pattern_domain_sizes[var_id]; ++i) {
            if (i != eff) {
                preconditions.emplace_back(var_id, i);
                effects.emplace_back(var_id, eff);
            } else {
                prevails.emplace_back(var_id, i);
            }
            multiply_out(hash_multipliers, pos + 1, conc_op_id,
                         prevails, preconditions, effects,
                         effects_without_pre, pattern_domain_sizes);
            if (i != eff) {
                preconditions.pop_back();
                effects.pop_back();
            } else {
                prevails.pop_back();
            }
        }
    }
}

void PatternEvaluator::build_abstract_operators(
    const vector<int> &hash_multipliers,
    const OperatorInfo &op,
    const vector<int> &variable_to_index,
    const vector<int> &pattern_domain_sizes) {
    // All variable value pairs that are a prevail condition
    vector<FactPair> prev_pairs;
    // All variable value pairs that are a precondition (value != -1)
    vector<FactPair> pre_pairs;
    // All variable value pairs that are an effect
    vector<FactPair> eff_pairs;
    // All variable value pairs that are a precondition (value = -1)
    vector<FactPair> effects_without_pre;

    int pattern_size = pattern_domain_sizes.size();
    vector<bool> has_precond_and_effect_on_var(pattern_size, false);
    vector<bool> has_precondition_on_var(pattern_size, false);

    for (const FactPair &pre : op.preconditions) {
        int pattern_var_id = variable_to_index[pre.var];
        if (pattern_var_id != -1) {
            has_precondition_on_var[pattern_var_id] = true;
        }
    }

    for (const FactPair &eff : op.effects) {
        int var_id = eff.var;
        int pattern_var_id = variable_to_index[var_id];
        int val = eff.value;
        if (pattern_var_id != -1) {
            if (has_precondition_on_var[pattern_var_id]) {
                has_precond_and_effect_on_var[pattern_var_id] = true;
                eff_pairs.emplace_back(pattern_var_id, val);
            } else {
                effects_without_pre.emplace_back(pattern_var_id, val);
            }
        }
    }

    for (const FactPair &pre : op.preconditions) {
        int var_id = pre.var;
        int pattern_var_id = variable_to_index[var_id];
        int val = pre.value;
        if (pattern_var_id != -1) { // variable occurs in pattern
            if (has_precond_and_effect_on_var[pattern_var_id]) {
                pre_pairs.emplace_back(pattern_var_id, val);
            } else {
                prev_pairs.emplace_back(pattern_var_id, val);
            }
        }
    }

    multiply_out(
        hash_multipliers, 0, op.concrete_operator_id, prev_pairs, pre_pairs,
        eff_pairs, effects_without_pre, pattern_domain_sizes);
}

bool PatternEvaluator::is_consistent(
    const vector<int> &hash_multipliers,
    const vector<int> &pattern_domain_sizes,
    int state_index,
    const vector<FactPair> &abstract_facts) const {
    for (const FactPair &abstract_goal : abstract_facts) {
        int pattern_var_id = abstract_goal.var;
        int temp = state_index / hash_multipliers[pattern_var_id];
        int val = temp % pattern_domain_sizes[pattern_var_id];
        if (val != abstract_goal.value) {
            return false;
        }
    }
    return true;
}

void PatternEvaluator::store_new_dead_ends(
    const Pattern &pattern,
    const vector<int> &distances,
    DeadEnds &dead_ends) const {
    const vector<int> &hash_multipliers = match_tree_backward->get_hash_multipliers();
    int pattern_size = hash_multipliers.size();
    for (size_t index = 0; index < distances.size(); ++index) {
        if (distances[index] == INF) {
            // Reverse index to partial state.
            vector<FactPair> partial_state;
            partial_state.reserve(pattern_size);
            int remaining_index = index;
            for (int i = pattern_size - 1; i >= 0; --i) {
                int var = pattern[i];
                int value = remaining_index / hash_multipliers[i];
                partial_state.emplace_back(var, value);
                remaining_index -= value * hash_multipliers[i];
            }
            reverse(partial_state.begin(), partial_state.end());
            if (!dead_ends.subsumes(partial_state)) {
                dead_ends.add(partial_state, task_info.domain_sizes);
            }
        }
    }
}

bool PatternEvaluator::is_useful(
    const Pattern &pattern,
    priority_queues::AdaptiveQueue<int> &pq,
    DeadEnds *dead_ends,
    const vector<int> &costs) const {
    assert(all_of(costs.begin(), costs.end(), [](int c) {return c >= 0;}));
    vector<int> distances(num_states, INF);
    int num_settled = 0;

    // Initialize queue.
    pq.clear();
    for (int goal : goal_states) {
        pq.push(0, goal);
        distances[goal] = 0;
    }

    // Reuse vector to save allocations.
    vector<int> applicable_operators;

    bool found_positive_finite_goal_distance = false;

    // Run Dijkstra loop.
    while (!pq.empty()) {
        pair<int, int> node = pq.pop();
        int distance = node.first;
        int state_index = node.second;
        assert(utils::in_bounds(state_index, distances));
        assert(distance != INF);
        if (distance > distances[state_index]) {
            continue;
        }
        ++num_settled;

        if (distance > 0) {
            found_positive_finite_goal_distance = true;
        }

        // Regress abstract state.
        applicable_operators.clear();
        match_tree_backward->get_applicable_operator_ids(state_index, applicable_operators);
        for (int abs_op_id : applicable_operators) {
            const AbstractBackwardOperator &op = abstract_backward_operators[abs_op_id];
            int predecessor = state_index + op.hash_effect;
            int conc_op_id = op.concrete_operator_id;
            assert(utils::in_bounds(conc_op_id, costs));
            int alternative_cost = (costs[conc_op_id] == INF) ?
                INF : distances[state_index] + costs[conc_op_id];
            assert(utils::in_bounds(predecessor, distances));
            if (alternative_cost < distances[predecessor]) {
                distances[predecessor] = alternative_cost;
                pq.push(alternative_cost, predecessor);
            }
        }
    }

    bool has_dead_end = (num_settled < num_states);
    assert(has_dead_end ==
           any_of(distances.begin(), distances.end(), [](int d) {return d == INF;}));
    if (dead_ends && has_dead_end) {
        // Add new dead ends to database.
        store_new_dead_ends(pattern, distances, *dead_ends);
    }
    return found_positive_finite_goal_distance;
}
}
