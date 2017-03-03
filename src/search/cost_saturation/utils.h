#ifndef COST_SATURATION_UTILS_H
#define COST_SATURATION_UTILS_H

#include "types.h"

#include <vector>

class TaskProxy;

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
extern std::vector<int> get_default_order(int num_abstractions);

extern int compute_sum_h(
    const std::vector<int> &local_state_ids,
    const std::vector<std::vector<int>> &h_values_by_abstraction);

std::vector<int> get_local_state_ids(
    const Abstractions &abstractions, const State &state);

extern std::vector<State> sample_states(
    const TaskProxy &task_proxy,
    const std::function<int (const State &)> &heuristic,
    int num_samples,
    utils::RandomNumberGenerator &rng);

extern void reduce_costs(
    std::vector<int> &remaining_costs,
    const std::vector<int> &saturated_costs);

extern void print_indexed_vector(const std::vector<int> &vec);

extern std::vector<bool> convert_to_bitvector(
    const std::vector<int> &vec, int size);
}

#endif
