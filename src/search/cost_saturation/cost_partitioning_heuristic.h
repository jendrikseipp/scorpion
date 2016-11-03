#ifndef COST_SATURATION_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class CostPartitioningHeuristic : public Heuristic {
    int compute_max_h_with_statistics(const std::vector<int> &local_state_ids) const;
    int compute_heuristic(const State &state);

protected:
    std::vector<std::vector<std::vector<int>>> h_values_by_order;
    std::vector<StateMap> state_maps;
    const bool debug;

    // For statistics.
    mutable std::vector<int> num_best_order;

    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit CostPartitioningHeuristic(const options::Options &opts);

    virtual void print_statistics() const override;
};

extern void add_common_cost_partitioning_options_to_parser(
    options::OptionParser &parser);
}

#endif
