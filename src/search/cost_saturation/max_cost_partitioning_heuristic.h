#ifndef COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H

#include "types.h"
#include "unsolvability_heuristic.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace cost_saturation {
class AbstractionFunction;
class CostPartitioningHeuristic;

/*
  Compute the maximum over multiple cost partitioning heuristics.
*/
class MaxCostPartitioningHeuristic : public Heuristic {
    std::vector<std::unique_ptr<AbstractionFunction>> abstraction_functions;
    std::vector<CostPartitioningHeuristic> cp_heuristics;
    std::unique_ptr<DeadEnds> dead_ends;
    UnsolvabilityHeuristic unsolvability_heuristic;

    // For statistics.
    mutable std::vector<int> num_best_order;

    void print_statistics() const;

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    MaxCostPartitioningHeuristic(
        Abstractions &&abstractions,
        std::vector<CostPartitioningHeuristic> &&cp_heuristics,
        std::unique_ptr<DeadEnds> &&dead_ends,
        const std::shared_ptr<AbstractTask> &transform,
        bool cache_estimates, const std::string &description,
        utils::Verbosity verbosity);
    virtual ~MaxCostPartitioningHeuristic() override;
};
}

#endif
