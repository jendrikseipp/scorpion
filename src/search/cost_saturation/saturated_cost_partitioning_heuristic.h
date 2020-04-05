#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include <vector>

namespace options {
class OptionParser;
class Options;
}

namespace cost_saturation {
class CostPartitioningHeuristic;

enum class Saturator {
    ALL,
    PERIM,
    PERIMSTAR,
};

extern CostPartitioningHeuristic compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const std::vector<int> &order,
    const std::vector<int> &costs,
    const std::vector<int> &abstract_state_ids);

extern CostPartitioningHeuristic compute_perim_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const std::vector<int> &order,
    const std::vector<int> &costs,
    const std::vector<int> &abstract_state_ids);

extern CostPartitioningHeuristic compute_perim_saturated_cost_partitioning_change_costs(
    const Abstractions &abstractions,
    const std::vector<int> &order,
    std::vector<int> &remaining_costs,
    const std::vector<int> &abstract_state_ids);

extern void add_saturator_option(options::OptionParser &parser);
extern CPFunction get_cp_function_from_options(const options::Options &opts);
}

#endif
