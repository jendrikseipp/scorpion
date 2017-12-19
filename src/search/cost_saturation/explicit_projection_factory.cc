#include "explicit_projection_factory.h"

#include "explicit_abstraction.h"
#include "types.h"

#include "../algorithms/ordered_set.h"
#include "../pdbs/match_tree.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <unordered_map>

using namespace std;

namespace cost_saturation {
static vector<int> compute_hash_multipliers(
    const TaskProxy &task_proxy, const pdbs::Pattern &pattern) {
    vector<int> hash_multipliers;
    hash_multipliers.reserve(pattern.size());
    int num_states = 1;
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
    return hash_multipliers;
}


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


ExplicitProjectionFactory::ExplicitProjectionFactory(
    const TaskProxy &task_proxy, const pdbs::Pattern &pattern)
    : task_proxy(task_proxy),
      pattern(pattern) {
    assert(utils::is_sorted_unique(pattern));

    hash_multipliers = compute_hash_multipliers(task_proxy, pattern);

    VariablesProxy variables = task_proxy.get_variables();
    vector<int> variable_to_index(variables.size(), -1);
    for (size_t i = 0; i < pattern.size(); ++i) {
        variable_to_index[pattern[i]] = i;
    }

    // Needs hash_multipliers.
    goal_states = compute_goal_states();
}

vector<int> ExplicitProjectionFactory::compute_goal_states() const {
    vector<int> goal_states;

    VariablesProxy variables = task_proxy.get_variables();
    vector<int> variable_to_index(variables.size(), -1);
    for (size_t i = 0; i < pattern.size(); ++i) {
        variable_to_index[pattern[i]] = i;
    }

    // compute abstract goal var-val pairs
    vector<FactPair> abstract_goals;
    for (FactProxy goal : task_proxy.get_goals()) {
        int var_id = goal.get_variable().get_id();
        int val = goal.get_value();
        if (variable_to_index[var_id] != -1) {
            abstract_goals.emplace_back(variable_to_index[var_id], val);
        }
    }

    for (int state_index = 0; state_index < num_states; ++state_index) {
        if (is_goal_state(state_index, abstract_goals, variables)) {
            goal_states.push_back(state_index);
        }
    }

    return goal_states;
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

int ExplicitProjectionFactory::hash_index(const State &state) const {
    int index = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        index += hash_multipliers[i] * state[pattern[i]].get_value();
    }
    return index;
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
