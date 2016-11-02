#ifndef COST_SATURATION_ZERO_ONE_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_ZERO_ONE_COST_PARTITIONING_HEURISTIC_H

#include "cost_partitioning_heuristic.h"

namespace cost_saturation {
class ZeroOneCostPartitioningHeuristic : public CostPartitioningHeuristic {
public:
    explicit ZeroOneCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
