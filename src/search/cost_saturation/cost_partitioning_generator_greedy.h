#ifndef COST_SATURATION_COST_PARTITIONING_GENERATOR_GREEDY_H
#define COST_SATURATION_COST_PARTITIONING_GENERATOR_GREEDY_H

#include "cost_partitioning_generator.h"

class State;
class SuccessorGenerator;

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class CostPartitioningGeneratorGreedy : public CostPartitioningGenerator {
    const bool use_random_initial_order;
    const bool increasing_ratios;
    const bool use_stolen_costs;
    const bool use_negative_costs;
    const bool queue_zero_ratios;
    const bool dynamic;
    const bool optimize;
    const bool steepest_ascent;
    const double max_optimization_time;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    // Unpartitioned h values.
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<double> used_costs_by_abstraction;

    std::vector<int> random_order;

protected:
    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs);

public:
    explicit CostPartitioningGeneratorGreedy(const options::Options &opts);

    virtual CostPartitioning get_next_cost_partitioning(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        CPFunction cp_function) override;
};
}

#endif
