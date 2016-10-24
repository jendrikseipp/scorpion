#ifndef CEGAR_SCP_GENERATORS_H
#define CEGAR_SCP_GENERATORS_H

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class Abstraction;

using CostPartitioning = std::vector<std::vector<int>>;
using CostPartitionings = std::vector<CostPartitioning>;


class SCPGenerator {
public:
    virtual CostPartitionings get_cost_partitionings(
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs) const = 0;
    virtual ~SCPGenerator() = default;
};


class RandomSCPGenerator : public SCPGenerator {
    const int num_orders;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

public:
    explicit RandomSCPGenerator(const options::Options &opts);

    virtual CostPartitionings get_cost_partitionings(
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs) const override;
};


class DiverseSCPGenerator : public SCPGenerator {
    const double max_time;
    const int num_samples = 1000;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

public:
    explicit DiverseSCPGenerator(const options::Options &opts);

    virtual CostPartitionings get_cost_partitionings(
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs) const override;
};

}

#endif
