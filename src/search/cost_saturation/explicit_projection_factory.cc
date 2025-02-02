#include "explicit_projection_factory.h"

#include "explicit_abstraction.h"
#include "projection.h"
#include "types.h"

#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <numeric>

using namespace std;

namespace cost_saturation {
static int get_pattern_index(const pdbs::Pattern &pattern, int var_id) {
    for (size_t pattern_index = 0; pattern_index < pattern.size(); ++pattern_index) {
        if (pattern[pattern_index] == var_id) {
            return pattern_index;
        }
    }
    return -1;
}

static vector<FactPair> get_projected_conditions(
    const ConditionsProxy &conditions, const pdbs::Pattern &pattern) {
    vector<FactPair> relevant_conditions;
    for (FactProxy fact : conditions) {
        int var_id = fact.get_variable().get_id();
        int pattern_index = get_pattern_index(pattern, var_id);
        if (pattern_index != -1) {
            relevant_conditions.emplace_back(pattern_index, fact.get_value());
        }
    }
    sort(relevant_conditions.begin(), relevant_conditions.end());
    relevant_conditions.erase(unique(relevant_conditions.begin(), relevant_conditions.end()),
                              relevant_conditions.end());
    assert(utils::is_sorted_unique(relevant_conditions));
    return relevant_conditions;
}


struct ProjectedEffect {
    FactPair fact;
    vector<FactPair> conditions;
    bool conditions_covered_by_pattern;

    ProjectedEffect(
        const FactPair &projected_fact,
        vector<FactPair> &&conditions,
        bool conditions_covered_by_pattern)
        : fact(projected_fact),
          conditions(move(conditions)),
          conditions_covered_by_pattern(conditions_covered_by_pattern) {
    }
    auto operator<=>(const ProjectedEffect &) const = default;

    friend ostream &operator<<(ostream &os, const ProjectedEffect &effect) {
        return os << effect.conditions << " --> " << effect.fact
                  << (effect.conditions_covered_by_pattern ? "!" : "?");
    }
};


ExplicitProjectionFactory::ExplicitProjectionFactory(
    const TaskProxy &task_proxy,
    const pdbs::Pattern &pattern)
    : task_proxy(task_proxy),
      pattern(pattern),
      looping_operators(task_proxy.get_operators().size(), false) {
    assert(utils::is_sorted_unique(pattern));

    VariablesProxy variables = task_proxy.get_variables();
    variable_to_pattern_index.resize(variables.size(), -1);
    for (size_t i = 0; i < pattern.size(); ++i) {
        variable_to_pattern_index[pattern[i]] = i;
    }

    domain_sizes.reserve(pattern.size());
    for (int var_id : pattern) {
        domain_sizes.push_back(variables[var_id].get_domain_size());
    }

    num_states = 1;
    hash_multipliers.reserve(pattern.size());
    for (int domain_size : domain_sizes) {
        hash_multipliers.push_back(num_states);
        if (utils::is_product_within_limit(
                num_states, domain_size, numeric_limits<int>::max())) {
            num_states *= domain_size;
        } else {
            cerr << "Given pattern is too large! (Overflow occured): " << endl;
            cerr << pattern << endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
    }

    compute_transitions();
    goal_states = rank_goal_states();
}

int ExplicitProjectionFactory::rank(const UnrankedState &state) const {
    int index = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        index += hash_multipliers[i] * state[i];
    }
    return index;
}

void ExplicitProjectionFactory::multiply_out_aux(
    const vector<FactPair> &partial_state, int partial_state_pos,
    UnrankedState &state, int state_pos,
    const function<void(const UnrankedState &)> &callback) const {
    if (state_pos == static_cast<int>(pattern.size())) {
        callback(state);
    } else if (partial_state_pos < static_cast<int>(partial_state.size()) &&
               partial_state[partial_state_pos].var == state_pos) {
        state[state_pos] = partial_state[partial_state_pos].value;
        multiply_out_aux(partial_state, partial_state_pos + 1, state, state_pos + 1, callback);
    } else {
        for (int value = 0; value < domain_sizes[state_pos]; ++value) {
            state[state_pos] = value;
            multiply_out_aux(partial_state, partial_state_pos, state, state_pos + 1, callback);
        }
    }
}

void ExplicitProjectionFactory::multiply_out(
    const vector<FactPair> &partial_state,
    const function<void(const UnrankedState &)> &callback) const {
    assert(utils::is_sorted_unique(partial_state));
    UnrankedState state(pattern.size());
    multiply_out_aux(partial_state, 0, state, 0, callback);
}

vector<int> ExplicitProjectionFactory::rank_goal_states() const {
    vector<FactPair> abstract_goals;
    for (FactProxy goal : task_proxy.get_goals()) {
        int var_id = goal.get_variable().get_id();
        int val = goal.get_value();
        if (variable_to_pattern_index[var_id] != -1) {
            abstract_goals.emplace_back(variable_to_pattern_index[var_id], val);
        }
    }

    vector<int> goal_states;
    if (abstract_goals.empty()) {
        /*
          In a projection to non-goal variables all states are goal states. We
          treat this as a special case to avoid unnecessary effort multiplying
          out all states.
        */
        goal_states.resize(num_states);
        iota(goal_states.begin(), goal_states.end(), 0);
    } else {
        sort(abstract_goals.begin(), abstract_goals.end());
        multiply_out(abstract_goals, [&](const UnrankedState &state) {
                         goal_states.push_back(rank(state));
                     });
    }
    return goal_states;
}

vector<ProjectedEffect> ExplicitProjectionFactory::get_projected_effects(
    const OperatorProxy &op) const {
    vector<ProjectedEffect> projected_effects;
    for (EffectProxy effect : op.get_effects()) {
        FactPair effect_fact = effect.get_fact().get_pair();
        int pattern_index = variable_to_pattern_index[effect_fact.var];
        if (pattern_index != -1) {
            EffectConditionsProxy original_conditions = effect.get_conditions();
            vector<FactPair> projected_conditions = get_projected_conditions(
                original_conditions, pattern);
            assert(projected_conditions.size() <= original_conditions.size());
            bool conditions_covered_by_pattern = (
                projected_conditions.size() == original_conditions.size());
            projected_effects.emplace_back(
                FactPair(pattern_index, effect_fact.value),
                move(projected_conditions),
                conditions_covered_by_pattern);
        }
    }
    projected_effects.erase(unique(projected_effects.begin(), projected_effects.end()),
                            projected_effects.end());
    sort(projected_effects.begin(), projected_effects.end());
    assert(utils::is_sorted_unique(projected_effects));
    return projected_effects;
}

bool ExplicitProjectionFactory::conditions_are_satisfied(
    const vector<FactPair> &conditions, const UnrankedState &state_values) const {
    return all_of(conditions.begin(), conditions.end(), [&state_values](const FactPair &precondition) {
                      return state_values[precondition.var] == precondition.value;
                  });
}

void ExplicitProjectionFactory::add_transition(
    int src_rank, int op_id, const UnrankedState &dest_values, bool debug) {
    int dest_rank = rank(dest_values);
    if (debug) {
        cout << "Add transition from " << src_rank << " to " << dest_rank << endl;
    }
    if (dest_rank == src_rank) {
        looping_operators[op_id] = true;
    } else {
        backward_graph[dest_rank].emplace_back(op_id, src_rank);
    }
}

void ExplicitProjectionFactory::add_transitions(
    const UnrankedState &src_values,
    int op_id,
    const vector<ProjectedEffect> &effects) {
    const bool debug = false;
    int src_rank = rank(src_values);
    UnrankedState base_dest_values = src_values;
    if (debug) {
        cout << endl;
        cout << "op: " << op_id << endl;
        cout << "source state: " << src_values << " -> " << src_rank << endl;
        for (const auto &effect : effects) {
            cout << effect << endl;
        }
    }

    vector<vector<FactPair>> possible_effects;
    // Loop over effects which are sorted by effect fact.
    for (const ProjectedEffect &effect : effects) {
        // Optimization: skip over no-op effects.
        if (src_values[effect.fact.var] == effect.fact.value) {
            continue;
        }
        if (conditions_are_satisfied(effect.conditions, src_values)) {
            if (effect.conditions_covered_by_pattern) {
                base_dest_values[effect.fact.var] = effect.fact.value;
            } else {
                // Store possible effects, grouped into buckets by variable.
                if (possible_effects.empty()) {
                    // Start first bucket.
                    possible_effects.push_back({effect.fact});
                } else {
                    FactPair last_fact = possible_effects.back().back();
                    if (last_fact == effect.fact) {
                        // Nothing to do for repeated fact.
                        continue;
                    } else if (last_fact.var == effect.fact.var) {
                        // Keep filling the same bucket.
                        possible_effects.back().push_back(effect.fact);
                    } else {
                        // Start new bucket.
                        possible_effects.push_back({effect.fact});
                    }
                }
            }
        }
    }
    if (debug) {
        cout << "variables with possible effects: " << possible_effects.size() << endl;
        cout << "base dest values: " << base_dest_values << endl;
    }

    // Remove all possible effects that would only set definite effects again.
    for (auto &facts : possible_effects) {
        for (auto it = facts.begin(); it != facts.end();) {
            if (base_dest_values[it->var] == it->value) {
                // Swap current fact with the last fact.
                *it = facts.back();
                facts.pop_back();
                // Do not increment the iterator, as we need to check the swapped-in fact.
            } else {
                ++it;
            }
        }
    }

    // Remove empty vectors.
    for (auto it = possible_effects.begin(); it != possible_effects.end();) {
        if (it->empty()) {
            // Swap current vector with the last vector.
            *it = move(possible_effects.back());
            possible_effects.pop_back();
            // Do not increment, as we need to check the swapped-in vector.
        } else {
            ++it;
        }
    }

    /* Add dummy fact for each variable with possible effects, signaling that no
       effect triggers for this variable. */
    for (auto &facts : possible_effects) {
        facts.push_back(FactPair::no_fact);
    }

    // Handle the case where all effects always trigger in this state.
    if (possible_effects.empty()) {
        add_transition(src_rank, op_id, base_dest_values);
        return;
    }

    // Apply all combinations of possible effects per variable and add transitions.
    vector<vector<FactPair>::iterator> iterators;
    iterators.reserve(possible_effects.size());
    for (auto &facts : possible_effects) {
        iterators.push_back(facts.begin());
    }
    int k = possible_effects.size();
    assert(k >= 1);
    while (iterators[0] != possible_effects[0].end()) {
        // Process the pointed-to facts.
        if (debug) {
            cout << "Possible facts: ";
            for (auto it : iterators) {
                cout << *it << " ";
            }
            cout << endl;
        }

        UnrankedState dest_values = base_dest_values;
        for (auto it : iterators) {
            FactPair fact = *it;
            if (fact != FactPair::no_fact) {
                dest_values[fact.var] = fact.value;
            }
        }
        add_transition(src_rank, op_id, dest_values, debug);

        // Increment the "counter" by 1.
        ++iterators[k - 1];
        for (int i = k - 1; (i > 0) && (iterators[i] == possible_effects[i].end()); --i) {
            iterators[i] = possible_effects[i].begin();
            ++iterators[i - 1];
        }
    }
}

void ExplicitProjectionFactory::compute_transitions() {
    backward_graph.resize(num_states);
    for (OperatorProxy op : task_proxy.get_operators()) {
        int op_id = op.get_id();
        auto preconditions = get_projected_conditions(op.get_preconditions(), pattern);
        auto effects = get_projected_effects(op);

        if (effects.empty()) {
            looping_operators[op_id] = true;
        } else {
            multiply_out(preconditions, [&](const UnrankedState &state) {
                             add_transitions(state, op_id, effects);
                         });
        }
    }

#ifndef NDEBUG
    for (const auto &transitions : backward_graph) {
        vector<Successor> copied_transitions = transitions;
        sort(copied_transitions.begin(), copied_transitions.end());
        assert(utils::is_sorted_unique(copied_transitions));
    }
#endif
}

unique_ptr<Abstraction> ExplicitProjectionFactory::convert_to_abstraction() {
    return utils::make_unique_ptr<ExplicitAbstraction>(
        utils::make_unique_ptr<ProjectionFunction>(pattern, move(hash_multipliers)),
        move(backward_graph),
        move(looping_operators),
        move(goal_states));
}
}
