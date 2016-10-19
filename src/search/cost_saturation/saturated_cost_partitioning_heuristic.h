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
    // TODO: Remove member.
    const std::vector<std::shared_ptr<AbstractionGenerator>> abstraction_generators;
    std::vector<std::vector<std::vector<int>>> h_values_by_order;
    std::vector<std::function<int (const State &)>> local_state_id_lookup_functions;

    // For statistics.
    mutable std::vector<int> num_best_order;

    std::vector<int> get_local_state_ids(const State &state) const;
    int compute_max_h_with_statistics(const std::vector<int> &local_state_ids) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;
    int compute_heuristic(const State &state);

public:
    explicit SaturatedCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
