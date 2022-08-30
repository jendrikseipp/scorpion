#ifndef COST_SATURATION_COST_PARTITIONING_HEURISTIC_COLLECTION_GENERATOR_H
#define COST_SATURATION_COST_PARTITIONING_HEURISTIC_COLLECTION_GENERATOR_H

#include "types.h"

#include <memory>
#include <vector>

class TaskProxy;

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class CostPartitioningHeuristic;
class OrderGenerator;

class CostPartitioningHeuristicCollectionGenerator {
    const std::shared_ptr<OrderGenerator> order_generator;
    const int max_orders;
    const int max_size_kb;
    const double max_time;
    const bool diversify;
    const int num_samples;
    const double max_optimization_time;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

public:
    CostPartitioningHeuristicCollectionGenerator(
        const std::shared_ptr<OrderGenerator> &order_generator,
        int max_orders,
        int max_size_kb,
        double max_time,
        bool diversify,
        int num_samples,
        double max_optimization_time,
        const std::shared_ptr<utils::RandomNumberGenerator> &rng);

    std::vector<CostPartitioningHeuristic> generate_cost_partitionings(
        const TaskProxy &task_proxy,
        const Abstractions &abstractions,
        const std::vector<int> &costs,
        const CPFunction &cp_function) const;
};
}

#endif
