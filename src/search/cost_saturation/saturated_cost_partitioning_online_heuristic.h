#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "cost_partitioning_heuristic.h"

namespace cost_saturation {
class SaturatedCostPartitioningOnlineHeuristic : public CostPartitioningHeuristic {
    int compute_heuristic(const State &state);

public:
    explicit SaturatedCostPartitioningOnlineHeuristic(const options::Options &opts);
};
}

#endif
