#ifndef COST_SATURATION_UTILS_H
#define COST_SATURATION_UTILS_H

#include "types.h"

#include <limits>
#include <vector>

class TaskProxy;

namespace cost_saturation {
extern int compute_sum_h(
    const std::vector<int> &local_state_ids,
    const std::vector<std::vector<int>> &h_values_by_abstraction);

std::vector<int> get_local_state_ids(
    const std::vector<StateMap> &state_maps, const State &state);

extern std::vector<State> sample_states(
    const TaskProxy task_proxy,
    std::function<int (const State &state)> heuristic,
    int num_samples);
}

#endif
