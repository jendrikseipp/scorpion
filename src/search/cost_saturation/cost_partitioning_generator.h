#ifndef COST_SATURATION_COST_PARTITIONING_GENERATOR_H
#define COST_SATURATION_COST_PARTITIONING_GENERATOR_H

#include "types.h"

#include <memory>
#include <vector>

class State;
class TaskProxy;

namespace options {
class Options;
class OptionParser;
}

namespace cost_saturation {
class Abstraction;

using CPFunction = std::function<CostPartitioning(
                                     const Abstractions &, const std::vector<int> &, const std::vector<int> &)>;

class CostPartitioningGenerator {
public:
    CostPartitioningGenerator() = default;
    virtual ~CostPartitioningGenerator() = default;

    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs) = 0;

    virtual CostPartitioning get_next_cost_partitioning(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        const State &state,
        CPFunction cp_function) = 0;

    virtual bool has_next_cost_partitioning() const {
        return true;
    }
};
}

#endif
