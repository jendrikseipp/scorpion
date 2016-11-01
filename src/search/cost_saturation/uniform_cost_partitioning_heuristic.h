#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_HEURISTIC_H

#include "abstraction_generator.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class AbstractionGenerator;

class UniformCostPartitioningHeuristic : public Heuristic {
    std::vector<std::vector<std::vector<int>>> h_values_by_order;
    std::vector<StateMap> state_maps;

    // For statistics.
    mutable std::vector<int> num_best_order;

    int compute_max_h_with_statistics(const std::vector<int> &local_state_ids) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;
    int compute_heuristic(const State &state);

public:
    explicit UniformCostPartitioningHeuristic(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif
