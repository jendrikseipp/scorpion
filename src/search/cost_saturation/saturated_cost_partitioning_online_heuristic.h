#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace cost_saturation {
class CostPartitioningGenerator;

class SaturatedCostPartitioningOnlineHeuristic : public Heuristic {
    const std::shared_ptr<CostPartitioningGenerator> cp_generator;
    const Abstractions abstractions;
    CPHeuristics cp_heuristics;
    const int interval;
    const bool store_cost_partitionings;
    const std::vector<int> costs;
    std::vector<std::vector<bool>> seen_facts;
    int num_evaluated_states;
    int num_scps_computed;

    // For statistics.
    mutable std::vector<int> num_best_order;

    bool should_compute_scp(const State &state);

protected:
    virtual int compute_heuristic(const GlobalState &state) override;

public:
    SaturatedCostPartitioningOnlineHeuristic(
        const options::Options &opts,
        Abstractions &&abstractions,
        CPHeuristics &&cp_heuristics);

    virtual void print_statistics() const override;
};
}

#endif
