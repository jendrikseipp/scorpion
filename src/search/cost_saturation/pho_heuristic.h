#ifndef COST_SATURATION_PHO_HEURISTIC_H
#define COST_SATURATION_PHO_HEURISTIC_H

#include "abstraction_generator.h"
#include "order_generator.h"
#include "types.h"
#include "uniform_cost_partitioning_heuristic.h"

#include "../lp/lp_solver.h"
#include "../utils/logging.h"

#include <memory>
#include <string>
#include <vector>

namespace cost_saturation {
class PhO {
    lp::LPSolver lp_solver;
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<bool> abstraction_has_unsolvable_states;
    utils::LogProxy log;

public:
    PhO(const Abstractions &abstractions, const std::vector<int> &costs,
        lp::LPSolverType solver_type, bool saturated,
        const utils::LogProxy &log);

    CostPartitioningHeuristic compute_cost_partitioning(
        const Abstractions &abstractions, const std::vector<int> &order,
        const std::vector<int> &costs,
        const std::vector<int> &abstract_state_ids);
};

/*
  Maximum over post-hoc optimization heuristics for different orders.
*/
class PhoHeuristic : public ScaledCostPartitioningHeuristic {
public:
    PhoHeuristic(
        const std::shared_ptr<AbstractTask> &task,
        const std::vector<std::shared_ptr<AbstractionGenerator>>
            &abstraction_generators,
        bool saturated, lp::LPSolverType lpsolver,
        const std::shared_ptr<OrderGenerator> &order_generator, int max_orders,
        int max_size_kb, double max_time, bool diversify, int num_samples,
        double max_optimization_time, int random_seed, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);
};
}

#endif
