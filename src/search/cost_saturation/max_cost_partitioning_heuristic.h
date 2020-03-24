#ifndef COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H

#include "types.h"
#include "unsolvability_heuristic.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace utils {
class Timer;
}

namespace cost_saturation {
class AbstractionFunction;
class CostPartitioningHeuristic;

/*
  Compute the maximum over multiple cost partitioning heuristics.
*/
class MaxCostPartitioningHeuristic : public Heuristic {
    std::vector<std::unique_ptr<AbstractionFunction>> abstraction_functions;
    std::vector<CostPartitioningHeuristic> cp_heuristics;
    UnsolvabilityHeuristic unsolvability_heuristic;

    // For statistics.
    mutable std::vector<int> num_best_order;
    std::unique_ptr<utils::Timer> compute_heuristic_timer;
    std::unique_ptr<utils::Timer> convert_global_state_timer;
    std::unique_ptr<utils::Timer> get_abstract_state_ids_timer;
    std::unique_ptr<utils::Timer> unsolvability_heuristic_timer;
    std::unique_ptr<utils::Timer> compute_max_h_timer;

    void print_statistics() const;
    int compute_heuristic(const State &state) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    MaxCostPartitioningHeuristic(
        const options::Options &opts,
        Abstractions abstractions,
        std::vector<CostPartitioningHeuristic> &&cp_heuristics,
        UnsolvabilityHeuristic &&unsolvability_heuristic);
    virtual ~MaxCostPartitioningHeuristic() override;
};
}

#endif
