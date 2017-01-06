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
    const bool increasing_ratios;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    std::unique_ptr<SuccessorGenerator> successor_generator;
    double average_operator_costs;
    std::unique_ptr<State> initial_state;
    int init_h;

    // Unpartitioned h values.
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<double> used_costs_by_abstraction;
    int min_used_costs;

    std::vector<int> random_order;
    std::vector<std::vector<int>> greedy_orders;
    std::vector<std::vector<int>> local_state_ids_by_sample;

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
