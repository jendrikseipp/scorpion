#include "projection.h"

#include "types.h"

#include "../task_proxy.h"

#include "../algorithms/priority_queues.h"
#include "../pdbs/match_tree.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <cassert>
#include <unordered_map>

using namespace std;

namespace cost_saturation {
static vector<int> get_abstract_preconditions(
    const vector<FactPair> &prev_pairs,
    const vector<FactPair> &pre_pairs,
    const vector<int> &hash_multipliers) {
    vector<int> abstract_preconditions(hash_multipliers.size(), -1);
    for (const FactPair &fact : prev_pairs) {
        int pattern_index = fact.var;
        abstract_preconditions[pattern_index] = fact.value;
    }
    for (const FactPair &fact : pre_pairs) {
        int pattern_index = fact.var;
        abstract_preconditions[pattern_index] = fact.value;
    }
    return abstract_preconditions;
}

static int compute_hash_effect(
    const vector<FactPair> &preconditions,
    const vector<FactPair> &effects,
    const vector<int> &hash_multipliers) {
    int hash_effect = 0;
    assert(preconditions.size() == effects.size());
    for (size_t i = 0; i < preconditions.size(); ++i) {
        int var = preconditions[i].var;
        assert(var == effects[i].var);
        int old_val = preconditions[i].value;
        int new_val = effects[i].value;
        assert(old_val != -1);
        int effect = (new_val - old_val) * hash_multipliers[var];
        hash_effect += effect;
    }
    return hash_effect;
}

static vector<int> get_variables(const OperatorProxy &op) {
    unordered_set<int> vars;
    vars.reserve(op.get_preconditions().size());
    for (FactProxy precondition : op.get_preconditions()) {
        vars.insert(precondition.get_variable().get_id());
    }
    for (EffectProxy effect : op.get_effects()) {
        vars.insert(effect.get_fact().get_variable().get_id());
    }
    vector<int> variables(vars.begin(), vars.end());
    sort(variables.begin(), variables.end());
    return variables;
}

static vector<int> get_changed_variables(const OperatorProxy &op) {
    unordered_map<int, int> var_to_precondition;
    for (FactProxy precondition : op.get_preconditions()) {
        const FactPair fact = precondition.get_pair();
        var_to_precondition[fact.var] = fact.value;
    }
    vector<int> changed_variables;
    for (EffectProxy effect : op.get_effects()) {
        const FactPair fact = effect.get_fact().get_pair();
        auto it = var_to_precondition.find(fact.var);
        if (it != var_to_precondition.end() && it->second != fact.value) {
            changed_variables.push_back(fact.var);
        }
    }
    sort(changed_variables.begin(), changed_variables.end());
    return changed_variables;
}

static vector<bool> compute_looping_operators(
    const TaskInfo &task_info, const pdbs::Pattern &pattern) {
    vector<bool> loops(task_info.get_num_operators());
    for (int op_id = 0; op_id < task_info.get_num_operators(); ++op_id) {
        loops[op_id] = task_info.operator_induces_self_loop(pattern, op_id);
    }
    return loops;
}

struct OperatorGroup {
    vector<FactPair> preconditions;
    vector<FactPair> effects;
    vector<int> operator_ids;

    bool operator<(const OperatorGroup &other) const {
        return operator_ids < other.operator_ids;
    }
};

using OperatorIDsByPreEffMap = utils::HashMap<pair<vector<FactPair>, vector<FactPair>>, vector<int>>;
using OperatorGroups = vector<OperatorGroup>;

static OperatorGroups group_equivalent_operators(
    const TaskProxy &task_proxy, const vector<int> &variable_to_pattern_index) {
    OperatorIDsByPreEffMap grouped_operator_ids;
    // Reuse vectors to save allocations.
    vector<FactPair> preconditions;
    vector<FactPair> effects;
    for (OperatorProxy op : task_proxy.get_operators()) {
        effects.clear();
        for (EffectProxy eff : op.get_effects()) {
            if (variable_to_pattern_index[eff.get_fact().get_pair().var] != -1) {
                effects.push_back(eff.get_fact().get_pair());
            }
        }
        /* Skip operators that only induce self-loops. They can be queried
           with operator_induces_self_loop(). */
        if (effects.empty()) {
            continue;
        }
        sort(effects.begin(), effects.end());

        preconditions.clear();
        for (FactProxy fact : op.get_preconditions()) {
            if (variable_to_pattern_index[fact.get_pair().var] != -1) {
                preconditions.push_back(fact.get_pair());
            }
        }
        sort(preconditions.begin(), preconditions.end());

        grouped_operator_ids[make_pair(move(preconditions), move(effects))].push_back(op.get_id());
    }
    OperatorGroups groups;
    for (auto &entry : grouped_operator_ids) {
        auto &pre_eff = entry.first;
        OperatorGroup group;
        group.preconditions = move(pre_eff.first);
        group.effects = move(pre_eff.second);
        group.operator_ids = move(entry.second);
        assert(utils::is_sorted_unique(group.operator_ids));
        groups.push_back(move(group));
    }
    // Sort by first operator ID for better cache locality.
    sort(groups.begin(), groups.end());
    return groups;
}

static OperatorGroups get_singleton_operator_groups(const TaskProxy &task_proxy) {
    OperatorGroups groups;
    for (OperatorProxy op : task_proxy.get_operators()) {
        OperatorGroup group;
        group.preconditions = task_properties::get_fact_pairs(op.get_preconditions());
        sort(group.preconditions.begin(), group.preconditions.end());
        group.effects.reserve(op.get_effects().size());
        for (EffectProxy eff : op.get_effects()) {
            group.effects.push_back(eff.get_fact().get_pair());
        }
        sort(group.effects.begin(), group.effects.end());
        group.operator_ids = {op.get_id()};
        groups.push_back(move(group));
    }
    return groups;
}


TaskInfo::TaskInfo(const TaskProxy &task_proxy) {
    num_variables = task_proxy.get_variables().size();
    num_operators = task_proxy.get_operators().size();
    goals = task_properties::get_fact_pairs(task_proxy.get_goals());
    mentioned_variables.resize(num_operators * num_variables, false);
    pre_eff_variables.resize(num_operators * num_variables, false);
    effect_variables.resize(num_operators * num_variables, false);
    for (OperatorProxy op : task_proxy.get_operators()) {
        for (int var : get_variables(op)) {
            mentioned_variables[get_index(op.get_id(), var)] = true;
        }
        for (int changed_var : get_changed_variables(op)) {
            pre_eff_variables[get_index(op.get_id(), changed_var)] = true;
        }
        for (EffectProxy effect : op.get_effects()) {
            int var = effect.get_fact().get_variable().get_id();
            effect_variables[get_index(op.get_id(), var)] = true;
        }
    }
}

const vector<FactPair> &TaskInfo::get_goals() const {
    return goals;
}

int TaskInfo::get_num_operators() const {
    return num_operators;
}

bool TaskInfo::operator_mentions_variable(int op_id, int var) const {
    return mentioned_variables[get_index(op_id, var)];
}

bool TaskInfo::operator_induces_self_loop(const pdbs::Pattern &pattern, int op_id) const {
    // Return false iff the operator has a precondition and effect for a pattern variable.
    for (int var : pattern) {
        if (pre_eff_variables[get_index(op_id, var)]) {
            return false;
        }
    }
    return true;
}

bool TaskInfo::operator_is_active(const pdbs::Pattern &pattern, int op_id) const {
    for (int var : pattern) {
        if (effect_variables[get_index(op_id, var)]) {
            return true;
        }
    }
    return false;
}


ProjectionFunction::ProjectionFunction(
    const pdbs::Pattern &pattern, const vector<int> &hash_multipliers) {
    assert(pattern.size() == hash_multipliers.size());
    variables_and_multipliers.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i) {
        variables_and_multipliers.emplace_back(pattern[i], hash_multipliers[i]);
    }
}

int ProjectionFunction::get_abstract_state_id(const State &concrete_state) const {
    int index = 0;
    for (const VariableAndMultiplier &pair : variables_and_multipliers) {
        index += pair.hash_multiplier * concrete_state[pair.pattern_var].get_value();
    }
    return index;
}


Projection::Projection(
    const TaskProxy &task_proxy,
    const shared_ptr<TaskInfo> &task_info,
    const pdbs::Pattern &pattern,
    bool combine_labels)
    : Abstraction(nullptr),
      task_info(task_info),
      pattern(pattern),
      looping_operators(compute_looping_operators(*task_info, pattern)) {
    assert(utils::is_sorted_unique(pattern));

    hash_multipliers.reserve(pattern.size());
    num_states = 1;
    for (int pattern_var_id : pattern) {
        hash_multipliers.push_back(num_states);
        VariableProxy var = task_proxy.get_variables()[pattern_var_id];
        if (utils::is_product_within_limit(num_states, var.get_domain_size(),
                                           numeric_limits<int>::max())) {
            num_states *= var.get_domain_size();
        } else {
            cerr << "Given pattern is too large! (Overflow occured): " << endl;
            cerr << pattern << endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
    }

    abstraction_function = utils::make_unique_ptr<ProjectionFunction>(
        pattern, hash_multipliers);

    VariablesProxy variables = task_proxy.get_variables();
    vector<int> variable_to_pattern_index(variables.size(), -1);
    for (size_t i = 0; i < pattern.size(); ++i) {
        variable_to_pattern_index[pattern[i]] = i;
    }
    pattern_domain_sizes.reserve(pattern.size());
    for (int pattern_var : pattern) {
        pattern_domain_sizes.push_back(variables[pattern_var].get_domain_size());
    }

    match_tree_backward = utils::make_unique_ptr<pdbs::MatchTree>(
        task_proxy, pattern, hash_multipliers);

    OperatorGroups operator_groups;
    if (combine_labels) {
        operator_groups = group_equivalent_operators(task_proxy, variable_to_pattern_index);
    } else {
        operator_groups = get_singleton_operator_groups(task_proxy);
    }
    int num_ops_covered_by_labels = 0;
    for (const auto &group : operator_groups) {
        num_ops_covered_by_labels += group.operator_ids.size();
    }
    label_to_operators.reserve(operator_groups.size(), num_ops_covered_by_labels);
    for (OperatorGroup &group : operator_groups) {
        const vector<FactPair> &preconditions = group.preconditions;
        const vector<FactPair> &effects = group.effects;

        int label_id = label_to_operators.size();
        label_to_operators.push_back(move(group.operator_ids));

        build_ranked_operators(
            preconditions, effects, variable_to_pattern_index, variables,
            [this, label_id](
                const vector<FactPair> &prevail,
                const vector<FactPair> &preconditions_,
                const vector<FactPair> &effects_,
                const vector<int> &hash_multipliers_) {
                vector<FactPair> regression_preconditions = prevail;
                regression_preconditions.insert(
                    regression_preconditions.end(), effects_.begin(), effects_.end());
                sort(regression_preconditions.begin(), regression_preconditions.end());
                int ranked_op_id = ranked_operators.size();
                match_tree_backward->insert(ranked_op_id, regression_preconditions);

                vector<int> abstract_preconditions = get_abstract_preconditions(
                    prevail, preconditions_, hash_multipliers_);
                int precondition_hash = 0;
                for (size_t pos = 0; pos < hash_multipliers_.size(); ++pos) {
                    int pre_val = abstract_preconditions[pos];
                    if (pre_val != -1) {
                        precondition_hash += hash_multipliers_[pos] * pre_val;
                    }
                }

                ranked_operators.emplace_back(
                    label_id,
                    precondition_hash,
                    compute_hash_effect(preconditions_, effects_, hash_multipliers_));
            });
    }
    ranked_operators.shrink_to_fit();

    goal_states = compute_goal_states(variable_to_pattern_index);
}

Projection::~Projection() {
}

bool Projection::increment_to_next_state(vector<FactPair> &facts) const {
    for (FactPair &fact : facts) {
        ++fact.value;
        if (fact.value > pattern_domain_sizes[fact.var] - 1) {
            fact.value = 0;
        } else {
            return true;
        }
    }
    return false;
}

vector<int> Projection::compute_goal_states(
    const vector<int> &variable_to_pattern_index) const {
    vector<FactPair> abstract_goals;
    for (FactPair goal : task_info->get_goals()) {
        if (variable_to_pattern_index[goal.var] != -1) {
            abstract_goals.emplace_back(
                variable_to_pattern_index[goal.var], goal.value);
        }
    }

    vector<int> goals;
    for (int state_index = 0; state_index < num_states; ++state_index) {
        if (is_consistent(state_index, abstract_goals)) {
            goals.push_back(state_index);
        }
    }
    return goals;
}

void Projection::multiply_out(int pos,
                              vector<FactPair> &prev_pairs,
                              vector<FactPair> &pre_pairs,
                              vector<FactPair> &eff_pairs,
                              const vector<FactPair> &effects_without_pre,
                              const VariablesProxy &variables,
                              const OperatorCallback &callback) const {
    if (pos == static_cast<int>(effects_without_pre.size())) {
        // All effects without precondition have been checked.
        if (!eff_pairs.empty()) {
            callback(prev_pairs, pre_pairs, eff_pairs, hash_multipliers);
        }
    } else {
        // For each possible value for the current variable, build an
        // abstract operator.
        int var_id = effects_without_pre[pos].var;
        int eff = effects_without_pre[pos].value;
        VariableProxy var = variables[pattern[var_id]];
        for (int i = 0; i < var.get_domain_size(); ++i) {
            if (i != eff) {
                pre_pairs.emplace_back(var_id, i);
                eff_pairs.emplace_back(var_id, eff);
            } else {
                prev_pairs.emplace_back(var_id, i);
            }
            multiply_out(pos + 1, prev_pairs, pre_pairs, eff_pairs,
                         effects_without_pre, variables, callback);
            if (i != eff) {
                pre_pairs.pop_back();
                eff_pairs.pop_back();
            } else {
                prev_pairs.pop_back();
            }
        }
    }
}

void Projection::build_ranked_operators(
    const vector<FactPair> &preconditions,
    const vector<FactPair> &effects,
    const vector<int> &variable_to_pattern_index,
    const VariablesProxy &variables,
    const OperatorCallback &callback) const {
    // All variable value pairs that are a prevail condition
    vector<FactPair> prev_pairs;
    // All variable value pairs that are a precondition (value != -1)
    vector<FactPair> pre_pairs;
    // All variable value pairs that are an effect
    vector<FactPair> eff_pairs;
    // All variable value pairs that are a precondition (value = -1)
    vector<FactPair> effects_without_pre;

    int num_vars = variables.size();
    vector<bool> has_precond_and_effect_on_var(num_vars, false);
    vector<bool> has_precondition_on_var(num_vars, false);

    for (FactPair pre : preconditions)
        has_precondition_on_var[pre.var] = true;

    for (FactPair eff : effects) {
        int var_id = eff.var;
        int pattern_var_id = variable_to_pattern_index[var_id];
        int val = eff.value;
        if (pattern_var_id != -1) {
            if (has_precondition_on_var[var_id]) {
                has_precond_and_effect_on_var[var_id] = true;
                eff_pairs.emplace_back(pattern_var_id, val);
            } else {
                effects_without_pre.emplace_back(pattern_var_id, val);
            }
        }
    }
    for (FactPair pre : preconditions) {
        int pattern_var_id = variable_to_pattern_index[pre.var];
        if (pattern_var_id != -1) { // variable occurs in pattern
            if (has_precond_and_effect_on_var[pre.var]) {
                pre_pairs.emplace_back(pattern_var_id, pre.value);
            } else {
                prev_pairs.emplace_back(pattern_var_id, pre.value);
            }
        }
    }
    multiply_out(0, prev_pairs, pre_pairs, eff_pairs,
                 effects_without_pre, variables, callback);
}

bool Projection::is_consistent(
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

vector<int> Projection::compute_saturated_costs(
    const vector<int> &h_values) const {
    int num_operators = get_num_operators();

    int num_labels = label_to_operators.size();
    vector<int> saturated_label_costs(num_labels, -INF);

    for_each_label_transition(
        [&saturated_label_costs, &h_values](const Transition &t) {
            assert(utils::in_bounds(t.src, h_values));
            assert(utils::in_bounds(t.target, h_values));
            int src_h = h_values[t.src];
            int target_h = h_values[t.target];
            if (src_h == INF || target_h == INF) {
                return;
            }
            int &needed_costs = saturated_label_costs[t.op];
            needed_costs = max(needed_costs, src_h - target_h);
        });

    vector<int> saturated_costs(num_operators, -INF);
    /* To prevent negative cost cycles, we ensure that all operators inducing
       self-loops (among possibly other transitions) have non-negative costs. */
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        if (operator_induces_self_loop(op_id)) {
            saturated_costs[op_id] = 0;
        }
    }

    for (int label_id = 0; label_id < num_labels; ++label_id) {
        int saturated_label_cost = saturated_label_costs[label_id];
        for (int op_id : label_to_operators.get_slice(label_id)) {
            saturated_costs[op_id] = max(saturated_costs[op_id], saturated_label_cost);
        }
    }

    return saturated_costs;
}

int Projection::get_num_operators() const {
    return task_info->get_num_operators();
}

vector<int> Projection::compute_goal_distances(const vector<int> &operator_costs) const {
    assert(all_of(operator_costs.begin(), operator_costs.end(), [](int c) {return c >= 0;}));

    // Assign each label the cost of cheapest operator that the label covers.
    int num_labels = label_to_operators.size();
    vector<int> label_costs;
    label_costs.reserve(num_labels);
    for (int label_id = 0; label_id < num_labels; ++label_id) {
        int min_cost = INF;
        for (int op_id : label_to_operators.get_slice(label_id)) {
            min_cost = min(min_cost, operator_costs[op_id]);
        }
        label_costs.push_back(min_cost);
    }

    vector<int> distances(num_states, INF);

    // Initialize queue.
    priority_queues::AdaptiveQueue<int> pq;
    for (int goal : goal_states) {
        pq.push(0, goal);
        distances[goal] = 0;
    }

    // Reuse vector to save allocations.
    vector<int> applicable_operators;

    // Run Dijkstra loop.
    while (!pq.empty()) {
        pair<int, size_t> node = pq.pop();
        int distance = node.first;
        int state_index = node.second;
        assert(utils::in_bounds(state_index, distances));
        if (distance > distances[state_index]) {
            continue;
        }

        // Regress abstract state.
        applicable_operators.clear();
        match_tree_backward->get_applicable_operator_ids(
            state_index, applicable_operators);
        for (int ranked_op_id : applicable_operators) {
            const RankedOperator &op = ranked_operators[ranked_op_id];
            int predecessor = state_index - op.hash_effect;
            assert(utils::in_bounds(op.label, label_costs));
            int alternative_cost = (label_costs[op.label] == INF) ?
                INF : distances[state_index] + label_costs[op.label];
            assert(utils::in_bounds(predecessor, distances));
            if (alternative_cost < distances[predecessor]) {
                distances[predecessor] = alternative_cost;
                pq.push(alternative_cost, predecessor);
            }
        }
    }
    return distances;
}

int Projection::get_num_states() const {
    return num_states;
}

bool Projection::operator_is_active(int op_id) const {
    return task_info->operator_is_active(pattern, op_id);
}

bool Projection::operator_induces_self_loop(int op_id) const {
    return looping_operators[op_id];
}

void Projection::for_each_transition(const TransitionCallback &callback) const {
    return for_each_label_transition(
        [this, &callback](const Transition &t) {
            for (int op_id : label_to_operators.get_slice(t.op)) {
                callback(Transition(t.src, op_id, t.target));
            }
        });
}

const vector<int> &Projection::get_goal_states() const {
    return goal_states;
}

const pdbs::Pattern &Projection::get_pattern() const {
    return pattern;
}

void Projection::dump() const {
    cout << "Ranked operators: " << ranked_operators.size()
         << ", goal states: " << goal_states.size() << "/" << num_states
         << endl;
}
}
