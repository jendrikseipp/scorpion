#ifndef COST_SATURATION_OPTIMAL_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_OPTIMAL_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include "../lp/lp_solver.h"

namespace cost_saturation {
/*
  Compute an optimal cost partitioning over abstraction heuristics.
*/
class OptimalCostPartitioningHeuristic : public Heuristic {
    const Abstractions abstractions;
    lp::LPSolver lp_solver;
    const bool allow_negative_costs;

    /*
      Column indices for abstraction variables indexed by abstraction id.
      Variable abstraction_variables[A] encodes the shortest distance of the
      current abstract state to its nearest abstract goal state in abstraction
      A using the cost partitioning.
    */
    std::vector<int> abstraction_variables;

    /*
      Column indices for distance variables indexed by abstraction id and
      abstract state id. Variable distance_variables[A][s] encodes the distance
      of abstract state s in abstraction A from the current abstract state
      using the cost partitioning.
    */
    std::vector<std::vector<int>> distance_variables;

    /*
      Column indices for operator cost variables indexed by abstraction id and
      operator id. Variable operator_cost_variables[A][o] encodes the cost of
      operator o in abstraction A.
    */
    std::vector<std::vector<int>> operator_cost_variables;

    std::vector<std::vector<int>> h_values;

    /*
      Cache the variables corresponding to the current state in all
      abstractions. This makes it easier to reset the bounds for each
      evaluation.
    */
    std::vector<int> current_abstract_state_vars;

    void generate_lp();
    void add_abstraction_variables(
        const Abstraction &abstraction,
        int abstraction_id,
        std::vector<lp::LPVariable> &lp_variables);
    void add_abstraction_constraints(
        const Abstraction &abstraction,
        int abstraction_id,
        std::vector<lp::LPConstraint> &lp_constraints);
    void add_operator_cost_constraints(
        std::vector<lp::LPConstraint> &lp_constraints);
    void release_memory();

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit OptimalCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
