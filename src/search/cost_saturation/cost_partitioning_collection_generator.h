#ifndef COST_SATURATION_COST_PARTITIONING_COLLECTION_GENERATOR_H
#define COST_SATURATION_COST_PARTITIONING_COLLECTION_GENERATOR_H

#include "types.h"

#include <memory>
#include <vector>

class RandomWalkSampler;
class State;
class SuccessorGenerator;
class TaskProxy;

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class Abstraction;
class CostPartitionedHeuristic;
class CostPartitioningGenerator;

class CostPartitioningCollectionGenerator {
    const std::shared_ptr<CostPartitioningGenerator> cp_generator;
    const int max_orders;
    const double max_time;
    const bool diversify;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    std::unique_ptr<RandomWalkSampler> sampler;
    CostPartitioning scp_for_sampling;
    int init_h;

    void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        CPFunction cp_function);

public:
    CostPartitioningCollectionGenerator(
        const std::shared_ptr<CostPartitioningGenerator> &cp_generator,
        int max_orders,
        double max_time,
        bool diversify,
        const std::shared_ptr<utils::RandomNumberGenerator> &rng);
    ~CostPartitioningCollectionGenerator();

    std::vector<CostPartitionedHeuristic> get_cost_partitionings(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        CPFunction cp_function,
        bool filter_zero_h_values);
};
}

#endif
