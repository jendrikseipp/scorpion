#ifndef COST_SATURATION_ORDER_GENERATOR_LOOKAHEAD_H
#define COST_SATURATION_ORDER_GENERATOR_LOOKAHEAD_H

#include "cost_partitioning_generator.h"

namespace cost_saturation {
class OrderGeneratorLookahead : public CostPartitioningGenerator {
    std::vector<int> original_costs;
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<std::vector<int>> saturated_costs_by_abstraction;

    const bool debug;
    int num_returned_orders;

    double get_fair_share(int abs1, int abs2, int op_id) const;
    double get_scaling_factor(int abs1, int abs2) const;
    double get_scaled_h(int abs1, int abs2, const std::vector<int> &local_state_ids) const;
    double estimate_h(int abs1, const std::vector<int> &local_state_ids) const;

public:
    explicit OrderGeneratorLookahead(const options::Options &opts);

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
}

#endif
