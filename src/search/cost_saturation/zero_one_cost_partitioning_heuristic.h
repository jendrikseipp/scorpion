#ifndef COST_SATURATION_ZERO_ONE_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_ZERO_ONE_COST_PARTITIONING_HEURISTIC_H

#include "abstraction_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "order_generator.h"
#include "types.h"

#include <memory>
#include <string>
#include <vector>

namespace cost_saturation {
/*
  Maximum over greedy zero-one cost partitioning heuristics for different
  orders.
*/
class ZeroOneCostPartitioningHeuristic : public MaxCostPartitioningHeuristic {
public:
    ZeroOneCostPartitioningHeuristic(
        const std::shared_ptr<AbstractTask> &task,
        const std::vector<std::shared_ptr<AbstractionGenerator>>
            &abstraction_generators,
        const std::shared_ptr<OrderGenerator> &order_generator, int max_orders,
        int max_size_kb, double max_time, bool diversify, int num_samples,
        double max_optimization_time, int random_seed, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);
};
}

#endif
