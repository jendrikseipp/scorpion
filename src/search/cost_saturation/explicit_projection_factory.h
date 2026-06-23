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
    void multiply_out_aux(
        const std::vector<FactPair> &partial_state, int partial_state_pos,
        UnrankedState &state, int state_pos,
        const std::function<void(const UnrankedState &)> &callback) const;
    void multiply_out(
        const std::vector<FactPair> &partial_state,
        const std::function<void(const UnrankedState &)> &callback) const;

    std::vector<ProjectedEffect> get_projected_effects(
        const OperatorProxy &op) const;
    bool conditions_are_satisfied(
        const std::vector<FactPair> &conditions,
        const UnrankedState &state_values) const;
    void add_transition(
        int src_rank, int op_id, const UnrankedState &dest_values,
        bool debug = false);
    void add_transitions(
        const UnrankedState &src_values, int op_id,
        const std::vector<ProjectedEffect> &effects);
    void compute_transitions();

    std::vector<int> rank_goal_states() const;

public:
    ExplicitProjectionFactory(
        const TaskProxy &task_proxy, const pdbs::Pattern &pattern);

    std::unique_ptr<Abstraction> convert_to_abstraction();
};
}

#endif
