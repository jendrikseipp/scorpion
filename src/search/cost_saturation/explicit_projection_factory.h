#ifndef COST_SATURATION_EXPLICIT_PROJECTION_FACTORY_H
#define COST_SATURATION_EXPLICIT_PROJECTION_FACTORY_H

#include "explicit_abstraction.h"

#include "../task_proxy.h"

#include "../pdbs/types.h"

#include <vector>

namespace cost_saturation {
struct ProjectedEffect;

class ExplicitProjectionFactory {
    using UnrankedState = std::vector<int>;

    TaskProxy task_proxy;
    const pdbs::Pattern pattern;
    const std::vector<std::vector<FactPair>> relevant_preconditions;
    std::vector<int> variable_to_pattern_index;
    std::vector<int> domain_sizes;

    std::vector<std::vector<Successor>> backward_graph;
    std::vector<bool> looping_operators;
    std::vector<int> goal_states;

    // size of the PDB
    int num_states;

    // multipliers for each variable for perfect hash function
    std::vector<int> hash_multipliers;

    int rank(const UnrankedState &state) const;
    int unrank(int rank, int pattern_index) const;
    UnrankedState unrank(int rank) const;

    std::vector<ProjectedEffect> get_projected_effects(const OperatorProxy &op) const;
    bool conditions_are_satisfied(
        const std::vector<FactPair> &conditions, const UnrankedState &state_values) const;
    bool is_applicable(const UnrankedState &state_values, int op_id) const;
    void add_transitions(
        const UnrankedState &src_values, int src_rank,
        int op_id, const std::vector<ProjectedEffect> &effects);
    void compute_transitions();

    std::vector<int> compute_goal_states() const;

    /*
      For a given abstract state (given as index), the according values
      for each variable in the state are computed and compared with the
      given pairs of goal variables and values. Returns true iff the
      state is a goal state.
    */
    bool is_goal_state(
        int state_index,
        const std::vector<FactPair> &abstract_goals,
        const VariablesProxy &variables) const;

public:
    ExplicitProjectionFactory(
        const TaskProxy &task_proxy,
        const pdbs::Pattern &pattern);

    std::unique_ptr<Abstraction> convert_to_abstraction();
};
}

#endif
