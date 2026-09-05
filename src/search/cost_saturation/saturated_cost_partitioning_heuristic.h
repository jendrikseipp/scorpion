#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "abstraction_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "order_generator.h"
#include "types.h"

#include <memory>
#include <vector>

namespace plugins {
class Feature;
class Options;
}

namespace cost_saturation {
class CostPartitioningHeuristic;

enum class Saturator {
    ALL,
    PERIM,
    PERIMSTAR,
};

/*
  Maximum over saturated cost partitioning heuristics for different orders.
*/
class SaturatedCostPartitioningHeuristic : public MaxCostPartitioningHeuristic {
public:
    SaturatedCostPartitioningHeuristic(
        const std::shared_ptr<AbstractTask> &task,
        const std::vector<std::shared_ptr<AbstractionGenerator>>
            &abstraction_generators,
        Saturator saturator,
        const std::shared_ptr<OrderGenerator> &order_generator, int max_orders,
        int max_size_kb, double max_time, bool diversify, int num_samples,
        double max_optimization_time, int random_seed, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);
};

extern CostPartitioningHeuristic compute_saturated_cost_partitioning(
    const Abstractions &abstractions, const std::vector<int> &order,
    std::vector<int> &remaining_costs,
    const std::vector<int> &abstract_state_ids);

extern CostPartitioningHeuristic compute_perim_saturated_cost_partitioning(
    const Abstractions &abstractions, const std::vector<int> &order,
    std::vector<int> &remaining_costs,
    const std::vector<int> &abstract_state_ids);

// Return the cost partitioning function for the given saturator.
extern CPFunction get_cp_function(Saturator saturator);

extern void add_saturator_option(plugins::Feature &feature);
extern CPFunction get_cp_function_from_options(const plugins::Options &options);
}

#endif
