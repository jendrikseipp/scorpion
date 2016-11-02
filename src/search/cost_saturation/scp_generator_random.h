#ifndef CEGAR_SCP_GENERATOR_RANDOM_H
#define CEGAR_SCP_GENERATOR_RANDOM_H

#include "scp_generator.h"

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class Abstraction;

class RandomSCPGenerator : public SCPGenerator {
    const std::shared_ptr<utils::RandomNumberGenerator> rng;
    std::vector<int> order;

protected:
    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs);

public:
    explicit RandomSCPGenerator(const options::Options &opts);

    virtual CostPartitioning get_next_cost_partitioning(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) override;
};
}

#endif
