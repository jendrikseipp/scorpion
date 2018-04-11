#ifndef COST_SATURATION_OPTIMAL_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_OPTIMAL_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include "../lp/lp_solver.h"

// TODO: Update comments.
// TODO: Add negative_costs parameter.

namespace cost_saturation {
class Abstraction;

class OptimalCostPartitioningHeuristic : public Heuristic {
    const Abstractions abstractions;
    lp::LPSolver lp_solver;
    const bool allow_negative_costs;

    // Column indices for heuristic variables indexed by PDB id.
    // The variable with id heuristic_variables[p] encodes the shortest distance
    // of the current abstract state to its nearest abstract goal state in pdb p
    // using the cost partitioning.
    std::vector<int> heuristic_variables;

    // Column indices for distance variables indexed by PDB id and abstract state id.
    // The variable with id distance_variables[p][s] encodes the distance of abstract
    // state s in pdb p from the current abstract state using the cost partitioning.
    std::vector<std::vector<int>> distance_variables;

    // Column indices for action cost variables indexed by PDB id and operator id.
    // The variable with id action_cost_variables[p][a] encodes cost action a should
    // have in pdb p.
    std::vector<std::vector<int>> action_cost_variables;

    std::vector<std::vector<int>> h_values;
    std::vector<std::vector<bool>> looping_operators;

    // Cache the variables corresponding to the current state in all pdbs.
    // This makes it easier to reset the bounds in each step.
    std::vector<int> current_abstract_state_vars;

    bool debug;

    void generate_lp();
    void introduce_abstraction_variables(
        const Abstraction &abstraction,
        int abstraction_id,
        std::vector<lp::LPVariable> &lp_variables);
    void add_abstraction_constraints(
        const Abstraction &abstraction,
        int abstraction_id,
        std::vector<lp::LPConstraint> &lp_constraints);
    void add_action_cost_constraints(std::vector<lp::LPConstraint> &lp_constraints);
    void release_memory();

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit OptimalCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
