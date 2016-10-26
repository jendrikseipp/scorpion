#ifndef CEGAR_SCP_GENERATORS_H
#define CEGAR_SCP_GENERATORS_H

#include "types.h"

#include <memory>
#include <vector>

class TaskProxy;

namespace options {
class Options;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class Abstraction;


class SCPGenerator {
protected:
    const int max_orders;
    const double max_time;
    const bool diversify;

public:
    SCPGenerator(const options::Options &opts);
    virtual ~SCPGenerator() = default;

    virtual CostPartitionings get_cost_partitionings(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) const = 0;
};


class DefaultSCPGenerator : public SCPGenerator {
public:
    explicit DefaultSCPGenerator(const options::Options &opts);

    virtual CostPartitionings get_cost_partitionings(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) const override;
};


class RandomSCPGenerator : public SCPGenerator {
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

public:
    explicit RandomSCPGenerator(const options::Options &opts);

    virtual CostPartitionings get_cost_partitionings(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) const override;
};


class GreedySCPGenerator : public SCPGenerator {
public:
    explicit GreedySCPGenerator(const options::Options &opts);

    virtual CostPartitionings get_cost_partitionings(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) const override;
};
}

#endif
