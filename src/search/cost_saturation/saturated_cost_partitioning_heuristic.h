#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include <vector>

namespace cost_saturation {
class CostPartitioningHeuristic;

extern CostPartitioningHeuristic compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const std::vector<int> &order,
    const std::vector<int> &costs);
}

#endif
