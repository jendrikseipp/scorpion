#ifndef COST_SATURATION_ORDER_GENERATOR_DYNAMIC_GREEDY_H
#define COST_SATURATION_ORDER_GENERATOR_DYNAMIC_GREEDY_H

#include "greedy_order_utils.h"
#include "order_generator.h"

namespace cost_saturation {
class OrderGeneratorDynamicGreedy : public OrderGenerator {
    const ScoringFunction scoring_function;

    const Abstractions *abstractions;
    const std::vector<int> *costs;

    Order compute_dynamic_greedy_order_for_sample(
        const std::vector<int> &abstract_state_ids,
        std::vector<int> remaining_costs) const;

public:
    OrderGeneratorDynamicGreedy(ScoringFunction scoring_function, int random_seed);

    virtual void initialize(
        const Abstractions &abstractions,
        const std::vector<int> &costs) override;

    virtual Order compute_order_for_state(
        const std::vector<int> &abstract_state_ids,
        bool verbose) override;
};
}

#endif
