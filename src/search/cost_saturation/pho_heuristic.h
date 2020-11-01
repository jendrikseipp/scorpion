#ifndef COST_SATURATION_PHO_HEURISTIC_H
#define COST_SATURATION_PHO_HEURISTIC_H

#include "abstraction.h"
#include "types.h"

#include "../lp/lp_solver.h"

#include <vector>

namespace cost_saturation {
class PhO {
    lp::LPSolver lp_solver;
    std::vector<std::vector<int>> saturated_costs_by_abstraction;
    std::vector<std::vector<int>> h_values_by_abstraction;
    bool saturated;
    bool debug;

public:
    PhO(
        const Abstractions &abstractions,
        const std::vector<int> &costs,
        lp::LPSolverType solver_type,
        bool saturated,
        bool debug = false);

    CostPartitioningHeuristic compute_cost_partitioning(
        const Abstractions &abstractions,
        const std::vector<int> &order,
        const std::vector<int> &costs,
        const std::vector<int> &abstract_state_ids);
};
}

#endif
