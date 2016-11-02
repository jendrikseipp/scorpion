#ifndef COST_SATURATION_ZERO_ONE_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_ZERO_ONE_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class ZeroOneCostPartitioningHeuristic : public Heuristic {
    std::vector<std::vector<std::vector<int>>> h_values_by_order;
    std::vector<StateMap> state_maps;
    const bool debug;

    int compute_max_h(const std::vector<int> &local_state_ids) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;
    int compute_heuristic(const State &state);

public:
    explicit ZeroOneCostPartitioningHeuristic(const options::Options &opts);
};
}

#endif
