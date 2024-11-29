#ifndef COST_SATURATION_UNIFORM_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_UNIFORM_COST_PARTITIONING_HEURISTIC_H

#include "max_cost_partitioning_heuristic.h"

#include <memory>

class AbstractTask;

namespace cost_saturation {
/*
  This class allows us to use real-values costs in cost partitionings with
  integers by scaling all costs by a constant factor.

  Users need to ensure that the task in the options is scaled by passing it to
  get_scaled_costs_task().
*/
class ScaledCostPartitioningHeuristic : public MaxCostPartitioningHeuristic {
protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    ScaledCostPartitioningHeuristic(
        Abstractions &&abstractions,
        std::vector<CostPartitioningHeuristic> &&cp_heuristics,
        std::unique_ptr<DeadEnds> &&dead_ends,
        const std::shared_ptr<AbstractTask> &transform,
        bool cache_estimates, const std::string &description,
        utils::Verbosity verbosity);
};


extern std::shared_ptr<AbstractTask> get_scaled_costs_task(
    const std::shared_ptr<AbstractTask> &task);
}

#endif
