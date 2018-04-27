#ifndef COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class CostPartitionedHeuristic;

class MaxCostPartitioningHeuristic : public Heuristic {
    Abstractions abstractions;
    const std::vector<CostPartitionedHeuristic> cp_heuristics;
    const bool debug;

    int compute_heuristic(const State &state) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    MaxCostPartitioningHeuristic(
        const options::Options &opts,
        Abstractions &&abstractions,
        std::vector<CostPartitionedHeuristic> &&cp_heuristics);
    virtual ~MaxCostPartitioningHeuristic() override;
};
}

#endif
