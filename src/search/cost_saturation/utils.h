#ifndef COST_SATURATION_UTILS_H
#define COST_SATURATION_UTILS_H

#include "types.h"

#include <iostream>
#include <vector>

class AbstractTask;
class State;

namespace options {
class OptionParser;
}

namespace cost_saturation {
class AbstractionGenerator;
class CostPartitioningHeuristic;

extern Abstractions generate_abstractions(
    const std::shared_ptr<AbstractTask> &task,
    const std::vector<std::shared_ptr<AbstractionGenerator>> &abstraction_generators);

extern Order get_default_order(int num_abstractions);

extern int compute_sum_h(
    const std::vector<int> &abstract_state_ids,
    const std::vector<std::vector<int>> &h_values_by_abstraction);

extern int compute_max_h_with_statistics(
    const CPHeuristics &cp_heuristics,
    const std::vector<int> &abstract_state_ids,
    std::vector<int> &num_best_order);

extern std::vector<int> get_abstract_state_ids(
    const Abstractions &abstractions, const State &state);

extern void reduce_costs(
    std::vector<int> &remaining_costs, const std::vector<int> &saturated_costs);

template<typename T>
void print_indexed_vector(const std::vector<T> &vec) {
    for (size_t i = 0; i < vec.size(); ++i) {
        std::cout << i << ":";
        T value = vec[i];
        if (value == INF) {
            std::cout << "inf";
        } else if (value == -INF) {
            std::cout << "-inf";
        } else {
            std::cout << value;
        }
        if (i < vec.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << std::endl;
}
}

#endif
