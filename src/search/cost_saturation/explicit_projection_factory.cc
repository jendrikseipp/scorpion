#include "explicit_projection_factory.h"

#include "explicit_abstraction.h"
#include "types.h"

#include "../algorithms/ordered_set.h"
#include "../pdbs/match_tree.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <unordered_map>

using namespace std;

namespace cost_saturation {
class StateMap {
    const pdbs::Pattern pattern;
    const std::vector<int> hash_multipliers;
public:
    StateMap(const pdbs::Pattern &pattern, std::vector<int> &&hash_multipliers)
        : pattern(pattern),
          hash_multipliers(move(hash_multipliers)) {
    }

    int operator()(const State &state) const {
        assert(pattern.size() == hash_multipliers.size());
        int index = 0;
        for (size_t i = 0; i < pattern.size(); ++i) {
            index += hash_multipliers[i] * state[pattern[i]].get_value();
        }
        return index;
    }
};


static vector<vector<FactPair>> get_relevant_preconditions_by_operator(
    const OperatorsProxy &ops, const pdbs::Pattern &pattern) {
    vector<vector<FactPair>> preconditions_by_operator;
    preconditions_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        vector<FactPair> relevant_preconditions;
        for (FactProxy fact : op.get_preconditions()) {
            int var_id = fact.get_variable().get_id();
            for (size_t pattern_index = 0; pattern_index < pattern.size(); ++pattern_index) {
                if (pattern[pattern_index] == var_id) {
                    relevant_preconditions.emplace_back(pattern_index, fact.get_value());
                }
            }
        }
        preconditions_by_operator.push_back(move(relevant_preconditions));
    }
    return preconditions_by_operator;
}


ExplicitProjectionFactory::ExplicitProjectionFactory(
    const TaskProxy &task_proxy, const pdbs::Pattern &pattern)
    : task_proxy(task_proxy),
      pattern(pattern),
      pattern_size(pattern.size()),
      num_operators(task_proxy.get_operators().size()),
      relevant_preconditions(
        get_relevant_preconditions_by_operator(task_proxy.get_operators(), pattern)) {
    assert(utils::is_sorted_unique(pattern));

    compute_hash_multipliers_and_num_states();

    VariablesProxy variables = task_proxy.get_variables();
    variable_to_pattern_index.resize(variables.size(), -1);
    for (size_t i = 0; i < pattern.size(); ++i) {
        variable_to_pattern_index[pattern[i]] = i;
    }

    domain_sizes.reserve(pattern.size());
    for (int var_id : pattern) {
        domain_sizes.push_back(variables[var_id].get_domain_size());
    }

    backward_graph.resize(num_states);
    compute_transitions();

    // Needs hash_multipliers.
    goal_states = compute_goal_states();
}

void ExplicitProjectionFactory::compute_hash_multipliers_and_num_states() {
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
            utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
        }
    }
}

vector<int> ExplicitProjectionFactory::compute_goal_states() const {
    vector<int> goal_states;

    // compute abstract goal var-val pairs
    vector<FactPair> abstract_goals;
    for (FactProxy goal : task_proxy.get_goals()) {
        int var_id = goal.get_variable().get_id();
        int val = goal.get_value();
        if (variable_to_pattern_index[var_id] != -1) {
            abstract_goals.emplace_back(variable_to_pattern_index[var_id], val);
        }
    }

    VariablesProxy variables = task_proxy.get_variables();
    for (int state_index = 0; state_index < num_states; ++state_index) {
        if (is_goal_state(state_index, abstract_goals, variables)) {
            goal_states.push_back(state_index);
        }
    }

    return goal_states;
}

int ExplicitProjectionFactory::rank(const UnrankedState &state) const {
    int index = 0;
    for (int i = 0; i < pattern_size; ++i) {
        index += hash_multipliers[i] * state[i];
    }
    return index;
}

int ExplicitProjectionFactory::unrank(int rank, int pattern_index) const {
    int temp = rank / hash_multipliers[pattern_index];
    return temp % domain_sizes[pattern_index];
}

ExplicitProjectionFactory::UnrankedState ExplicitProjectionFactory::unrank(int rank) const {
    UnrankedState values;
    values.reserve(pattern.size());
    for (int pattern_index = 0; pattern_index < pattern_size; ++pattern_index) {
        values.push_back(unrank(rank, pattern_index));
    }
    return values;
}

bool ExplicitProjectionFactory::is_applicable(UnrankedState &state_values, int op_id) {
    const vector<FactPair> &preconditions = relevant_preconditions[op_id];
    for (const FactPair &precondition : preconditions) {
        if (state_values[precondition.var] != precondition.value) {
            return false;
        }
    }
    return true;
}

void ExplicitProjectionFactory::add_transitions(
    const UnrankedState &src_values, int src_rank, int op_id) {
    OperatorProxy op = task_proxy.get_operators()[op_id];
    UnrankedState dest_values = src_values;
    for (EffectProxy effect : op.get_effects()) {
        FactPair effect_fact = effect.get_fact().get_pair();
        int pattern_pos = variable_to_pattern_index[effect_fact.var];
        if (pattern_pos != -1) {
            dest_values[pattern_pos] = effect_fact.value;
        }
    }
    int dest_rank = rank(dest_values);
    if (dest_rank == src_rank) {
        looping_operators.insert(op_id);
    } else {
        backward_graph[dest_rank].push_back(Transition(op_id, src_rank));
    }
}

void ExplicitProjectionFactory::compute_transitions() {
    for (int src_rank = 0; src_rank < num_states; ++src_rank) {
        vector<int> src_values = unrank(src_rank);
        for (int op_id = 0; op_id < num_operators; ++op_id) {
            if (is_applicable(src_values, op_id)) {
                add_transitions(src_values, src_rank, op_id);
            }
        }
    }
}

bool ExplicitProjectionFactory::is_goal_state(
    int state_index,
    const vector<FactPair> &abstract_goals,
    const VariablesProxy &variables) const {
    for (const FactPair &abstract_goal : abstract_goals) {
        int pattern_var_id = abstract_goal.var;
        int var_id = pattern[pattern_var_id];
        VariableProxy var = variables[var_id];
        int temp = state_index / hash_multipliers[pattern_var_id];
        int val = temp % var.get_domain_size();
        if (val != abstract_goal.value) {
            return false;
        }
    }
    return true;
}

unique_ptr<Abstraction> ExplicitProjectionFactory::convert_to_abstraction() {
    return utils::make_unique_ptr<ExplicitAbstraction>(
        StateMap(pattern, move(hash_multipliers)),
        move(backward_graph),
        looping_operators.pop_as_vector(),
        move(goal_states),
        num_operators);
}
}
