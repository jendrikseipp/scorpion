#ifndef COST_SATURATION_UTILS_H
#define COST_SATURATION_UTILS_H

#include "abstraction.h"
#include "types.h"

#include <execution>
#include <iostream>
#include <vector>

class AbstractTask;
class Evaluator;
class State;

namespace plugins {
class Feature;
class Options;
}

namespace cost_saturation {
class AbstractionGenerator;
class CostPartitioningHeuristicCollectionGenerator;
class MaxCostPartitioningHeuristic;
class UnsolvabilityHeuristic;

extern Abstractions generate_abstractions(
    const std::shared_ptr<AbstractTask> &task,
    const std::vector<std::shared_ptr<AbstractionGenerator>> &abstraction_generators,
    DeadEnds *dead_ends = nullptr);

extern AbstractionFunctions extract_abstraction_functions_from_useful_abstractions(
    const std::vector<CostPartitioningHeuristic> &cp_heuristics,
    const UnsolvabilityHeuristic *unsolvability_heuristic,
    Abstractions &abstractions);

extern Order get_default_order(int num_abstractions);

extern bool is_sum_within_range(int a, int b);

// The sum of mixed infinities evaluates to the left infinite value.
extern int left_addition(int a, int b);

extern int compute_max_h(
    const CPHeuristics &cp_heuristics,
    const std::vector<int> &abstract_state_ids,
    std::vector<int> *num_best_order = nullptr);

template<typename AbstractionsOrFunction>
std::vector<int> get_abstract_state_ids(
    const std::vector<AbstractionsOrFunction> &abstractions, const State &state) {
    std::vector<int> abstract_state_ids(abstractions.size(), -2);
    // Only add local state IDs for useful abstractions and use dummy value if abstraction will never be used.
    auto get_abs_state_id = [&state](const AbstractionsOrFunction &abstraction) {
            return abstraction ? abstraction->get_abstract_state_id(state) : -1;
        };
    std::transform(
        std::execution::unseq,
        abstractions.cbegin(), abstractions.cend(),
        abstract_state_ids.begin(), get_abs_state_id);
    return abstract_state_ids;
}

extern void reduce_costs(
    std::vector<int> &remaining_costs, const std::vector<int> &saturated_costs);


extern void add_order_options(plugins::Feature &feature);
extern void add_options_for_cost_partitioning_heuristic(
    plugins::Feature &feature, const std::string &description, bool consistent = true);
extern std::shared_ptr<MaxCostPartitioningHeuristic> get_max_cp_heuristic(
    const plugins::Options &opts, const CPFunction &cp_function);
extern std::shared_ptr<CostPartitioningHeuristicCollectionGenerator>
get_cp_heuristic_collection_generator_from_options(const plugins::Options &opts);

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
