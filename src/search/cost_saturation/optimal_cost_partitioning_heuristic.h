#ifndef COST_SATURATION_OPTIMAL_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_OPTIMAL_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include "../algorithms/named_vector.h"
#include "../lp/lp_solver.h"

namespace cost_saturation {
using LPConstraints = named_vector::NamedVector<lp::LPConstraint>;
using LPVariables = named_vector::NamedVector<lp::LPVariable>;
/*
  Compute an optimal cost partitioning over abstraction heuristics.
*/
class OptimalCostPartitioningHeuristic : public Heuristic {
    AbstractionFunctions abstraction_functions;
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
      abstractions. This speeds up resetting the bounds for each evaluation.
    */
    std::vector<int> current_abstract_state_vars;

    void generate_lp(const Abstractions &abstractions);
    void add_abstraction_variables(
        const Abstraction &abstraction,
        int abstraction_id,
        LPVariables &lp_variables);
    void add_abstraction_constraints(
        const Abstraction &abstraction,
        int abstraction_id,
        LPConstraints &lp_constraints);
    void add_operator_cost_constraints(
        LPConstraints &lp_constraints);
    void release_memory();

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    OptimalCostPartitioningHeuristic(
        const std::vector<std::shared_ptr<AbstractionGenerator>> &abstraction_generators,
        bool allow_negative_costs, lp::LPSolverType lpsolver,
        const std::shared_ptr<AbstractTask> &transform,
        bool cache_estimates, const std::string &description,
        utils::Verbosity verbosity);
};
}

#endif
