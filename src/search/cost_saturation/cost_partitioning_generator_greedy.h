#ifndef COST_SATURATION_COST_PARTITIONING_GENERATOR_GREEDY_H
#define COST_SATURATION_COST_PARTITIONING_GENERATOR_GREEDY_H

#include "cost_partitioning_generator.h"

namespace utils {
class CountdownTimer;
}

namespace cost_saturation {
class CostPartitioningGeneratorGreedy : public CostPartitioningGenerator {
    const bool use_random_initial_order;
    const bool reverse_initial_order;
    const bool use_negative_costs;
    const bool queue_zero_ratios;
    bool dynamic;
    const bool hybrid;
    const double max_greedy_time;
    const bool steepest_ascent;
    const bool continue_after_switch;
    const double max_optimization_time;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    // Unpartitioned h values.
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<double> used_costs_by_abstraction;

    std::vector<int> random_order;

    int num_returned_orders;

    bool search_improving_successor(
        CPFunction cp_function,
        const utils::CountdownTimer &timer,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        const std::vector<int> &local_state_ids,
        std::vector<int> &incumbent_order,
        int &incumbent_h_value,
        bool verbose) const;

    void do_hill_climbing(
        CPFunction cp_function,
        const utils::CountdownTimer &timer,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        const std::vector<int> &local_state_ids,
        std::vector<int> &incumbent_order,
        bool verbose) const;

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
        const State &state,
        CPFunction cp_function) override;
};
}

#endif
