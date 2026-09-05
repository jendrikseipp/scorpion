#ifndef COST_SATURATION_ORDER_GENERATOR_H
#define COST_SATURATION_ORDER_GENERATOR_H

#include "types.h"

#include "../component.h"

#include <vector>

namespace plugins {
class Feature;
class Options;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class OrderGenerator : public components::TaskSpecificComponent {
protected:
    const std::shared_ptr<utils::RandomNumberGenerator> rng;
public:
    OrderGenerator(const std::shared_ptr<AbstractTask> &task, int random_seed);

    virtual void initialize(
        const Abstractions &abstractions, const std::vector<int> &costs) = 0;

    virtual Order compute_order_for_state(
        const std::vector<int> &abstract_state_ids, bool verbose) = 0;
};

using TaskIndependentOrderGenerator =
    components::TaskIndependentComponent<OrderGenerator>;

extern void add_order_generator_arguments_to_feature(plugins::Feature &feature);
extern std::tuple<int> get_order_generator_arguments_from_options(
    const plugins::Options &opts);
}

#endif
