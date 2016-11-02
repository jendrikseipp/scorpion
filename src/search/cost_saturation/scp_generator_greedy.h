#ifndef COST_SATURATION_SCP_GENERATOR_GREEDY_H
#define COST_SATURATION_SCP_GENERATOR_GREEDY_H

#include "scp_generator.h"

namespace cost_saturation {
class SCPGeneratorGreedy : public SCPGenerator {
    const bool increasing_ratios;

protected:
    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs);

public:
    explicit SCPGeneratorGreedy(const options::Options &opts);

    virtual CostPartitioning get_next_cost_partitioning(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) override;
};
}

#endif
