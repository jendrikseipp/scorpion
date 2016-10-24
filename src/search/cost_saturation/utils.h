#ifndef COST_SATURATION_UTILS_H
#define COST_SATURATION_UTILS_H

#include <limits>
#include <vector>

namespace cost_saturation {
extern int compute_sum_h(
    const std::vector<int> &local_state_ids,
    const std::vector<std::vector<int>> &h_values_by_abstraction);
}

#endif
