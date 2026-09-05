#ifndef COST_SATURATION_UNIFORM_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_UNIFORM_COST_PARTITIONING_HEURISTIC_H

#include "abstraction_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "order_generator.h"

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
    /*
      The heuristic operates on a copy of the task in which all operator costs
      are scaled by a constant factor.
    */
    ScaledCostPartitioningHeuristic(
        const std::shared_ptr<AbstractTask> &task, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);
};

/*
  (Opportunistic) uniform cost partitioning heuristic.
*/
class UniformCostPartitioningHeuristic
    : public ScaledCostPartitioningHeuristic {
public:
    UniformCostPartitioningHeuristic(
        const std::shared_ptr<AbstractTask> &task,
        const std::vector<std::shared_ptr<AbstractionGenerator>>
            &abstraction_generators,
        bool opportunistic, bool debug,
        const std::shared_ptr<OrderGenerator> &order_generator, int max_orders,
        int max_size_kb, double max_time, bool diversify, int num_samples,
        double max_optimization_time, int random_seed, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);
};

extern std::shared_ptr<AbstractTask> get_scaled_costs_task(
    const std::shared_ptr<AbstractTask> &task);
}

#endif
