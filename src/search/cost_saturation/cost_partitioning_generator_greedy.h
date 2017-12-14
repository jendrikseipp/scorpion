#ifndef COST_SATURATION_COST_PARTITIONING_GENERATOR_GREEDY_H
#define COST_SATURATION_COST_PARTITIONING_GENERATOR_GREEDY_H

#include "cost_partitioning_generator.h"

namespace utils {
class CountdownTimer;
class RandomNumberGenerator;
}

namespace cost_saturation {
enum class ScoringFunction {
    RANDOM,
    MAX_HEURISTIC,
    MIN_COSTS,
    MAX_HEURISTIC_PER_COSTS,
    MIN_STOLEN_COSTS,
    MAX_HEURISTIC_PER_STOLEN_COSTS
};

class CostPartitioningGeneratorGreedy : public CostPartitioningGenerator {
    const bool reverse_order;
    const ScoringFunction scoring_function;
    const bool use_negative_costs;
    const bool use_exp;
    const bool dynamic;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    // Unpartitioned h values.
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<int> used_costs_by_abstraction;
    std::vector<int> stolen_costs_by_abstraction;

    std::vector<int> random_order;

    int num_returned_orders;

    long double rate_abstraction(
        const std::vector<int> &local_state_ids, int abs_id) const;
    Order compute_static_greedy_order_for_sample(
        const std::vector<int> &local_state_ids, bool verbose) const;
    Order compute_greedy_dynamic_order_for_sample(
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &local_state_ids,
        std::vector<int> remaining_costs) const;

public:
    explicit CostPartitioningGeneratorGreedy(const options::Options &opts);

    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs) override;

    virtual Order get_next_order(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        const std::vector<int> &local_state_ids,
        bool verbose) override;
};

void add_scoring_function_to_parser(options::OptionParser &parser);
}

#endif
