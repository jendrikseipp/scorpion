#include "cost_partitioned_heuristic.h"

#include "../utils/collections.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
CostPartitionedHeuristicValues::CostPartitionedHeuristicValues(
    int heuristic_index, std::vector<int> &&h_values)
    : heuristic_index(heuristic_index),
      h_values(move(h_values)) {
}


CostPartitionedHeuristic::CostPartitionedHeuristic(CostPartitioning &&cp) {
    int num_heuristics = cp.size();
    for (int h_id = 0; h_id < num_heuristics; ++h_id) {
        vector<int> &h_values = cp[h_id];
        bool all_zero = all_of(h_values.begin(), h_values.end(), [](int i){return i == 0;});
        if (!all_zero) {
            h_values_by_heuristic.emplace_back(h_id, move(h_values));
        }
    }
}

int CostPartitionedHeuristic::compute_heuristic(const vector<int> &local_state_ids) const {
    int sum_h = 0;
    for (const CostPartitionedHeuristicValues &cp_h_values : h_values_by_heuristic) {
        assert(utils::in_bounds(cp_h_values.heuristic_index, local_state_ids));
        int state_id = local_state_ids[cp_h_values.heuristic_index];
        assert(utils::in_bounds(state_id, cp_h_values.h_values));
        sum_h += cp_h_values.h_values[state_id];
    }
    return sum_h;
}
}
