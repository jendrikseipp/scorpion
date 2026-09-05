#ifndef COST_SATURATION_UTILS_H
#define COST_SATURATION_UTILS_H

#include "abstraction.h"
#include "abstraction_generator.h"
#include "order_generator.h"
#include "types.h"

#include <algorithm>
#include <iostream>
#include <vector>

class AbstractTask;
class Evaluator;
class State;
class TaskProxy;

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
    const std::vector<std::shared_ptr<AbstractionGenerator>>
        &abstraction_generators,
    DeadEnds *dead_ends = nullptr);

extern AbstractionFunctions
extract_abstraction_functions_from_useful_abstractions(
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
    const std::vector<AbstractionsOrFunction> &abstractions,
    const State &state) {
    std::vector<int> abstract_state_ids(abstractions.size(), -2);
    // Only add local state IDs for useful abstractions and use dummy value if
    // abstraction will never be used.
    auto get_abs_state_id =
        [&state](const AbstractionsOrFunction &abstraction) {
            return abstraction ? abstraction->get_abstract_state_id(state) : -1;
        };
    std::transform(
        abstractions.cbegin(), abstractions.cend(), abstract_state_ids.begin(),
        get_abs_state_id);
    return abstract_state_ids;
}

extern void reduce_costs(
    std::vector<int> &remaining_costs, const std::vector<int> &saturated_costs);

// Determine whether to use explicit transitions based on the transition type
// and task properties. AUTO mode uses explicit transitions if the task has
// conditional effects, implicit otherwise.
extern bool use_explicit_transitions(
    TransitionSystemType transition_type, const TaskProxy &task_proxy);

extern void add_transition_type_option(plugins::Feature &feature);
extern void add_order_options(plugins::Feature &feature);
extern void add_options_for_cost_partitioning_heuristic(
    plugins::Feature &feature, const std::string &description,
    bool consistent = true);

/*
  Arguments added by add_order_options(), in the order in which the
  corresponding constructor parameters have to be declared:
  order generator, max_orders, max_size, max_time, diversify, samples,
  max_optimization_time and the random seed.
*/
using OrderArguments = std::tuple<
    std::shared_ptr<TaskIndependentOrderGenerator>, int, int, double, bool, int,
    double, int>;
extern OrderArguments get_order_arguments_from_options(
    const plugins::Options &opts);

/*
  Abstraction generators and heuristic options are only added once, so we
  bundle the arguments added by add_options_for_cost_partitioning_heuristic().
*/
extern std::vector<std::shared_ptr<TaskIndependentAbstractionGenerator>>
get_abstraction_generator_list_from_options(const plugins::Options &opts);

/*
  Compute cost partitioning heuristics for the given abstractions using the
  options added by add_order_options().
*/
extern CPHeuristics compute_cp_heuristics(
    const TaskProxy &task_proxy, const Abstractions &abstractions,
    const std::vector<int> &costs, const CPFunction &cp_function,
    const std::shared_ptr<OrderGenerator> &order_generator, int max_orders,
    int max_size_kb, double max_time, bool diversify, int num_samples,
    double max_optimization_time, int random_seed);

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
