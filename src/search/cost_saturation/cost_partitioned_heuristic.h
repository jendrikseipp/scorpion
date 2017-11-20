#ifndef COST_SATURATION_COST_PARTITIONED_HEURISTIC_H
#define COST_SATURATION_COST_PARTITIONED_HEURISTIC_H

#include "types.h"

#include <vector>

namespace cost_saturation {
struct CostPartitionedHeuristicValues {
    const int heuristic_index;
    const std::vector<int> h_values;

public:
    CostPartitionedHeuristicValues(int heuristic_index, std::vector<int> &&h_values);
};


class CostPartitionedHeuristic {
    std::vector<CostPartitionedHeuristicValues> h_values_by_heuristic;

public:
    explicit CostPartitionedHeuristic(CostPartitioning &&cp);

    int compute_heuristic(const std::vector<int> &local_state_ids) const;
};
}

#endif
