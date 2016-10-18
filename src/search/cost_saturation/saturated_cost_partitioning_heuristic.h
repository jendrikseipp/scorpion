#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class AbstractionGenerator;

class SaturatedCostPartitioningHeuristic : public Heuristic {
    const std::vector<std::shared_ptr<AbstractionGenerator>> abstraction_generators;
    std::vector<std::vector<int>> h_values_by_abstraction;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;
    int compute_heuristic(const State &state);

public:
    explicit SaturatedCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
