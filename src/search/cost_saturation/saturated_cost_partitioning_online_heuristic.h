#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "cost_partitioning_generator.h"
#include "cost_partitioning_heuristic.h"

#include <memory>
#include <vector>

namespace cost_saturation {
class SaturatedCostPartitioningOnlineHeuristic : public CostPartitioningHeuristic {
    std::shared_ptr<CostPartitioningGenerator> cp_generator;
    const std::vector<int> costs;

    virtual int compute_heuristic(const State &state) override;

public:
    explicit SaturatedCostPartitioningOnlineHeuristic(const options::Options &opts);
};
}

#endif
