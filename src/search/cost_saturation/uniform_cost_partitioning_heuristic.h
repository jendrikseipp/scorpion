#ifndef COST_SATURATION_UNIFORM_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_UNIFORM_COST_PARTITIONING_HEURISTIC_H

#include "cost_partitioning_heuristic.h"

namespace cost_saturation {
class UniformCostPartitioningHeuristic : public CostPartitioningHeuristic {
protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit UniformCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
