#ifndef COST_SATURATION_COST_PARTITIONING_GENERATOR_H
#define COST_SATURATION_COST_PARTITIONING_GENERATOR_H

#include "types.h"

#include <memory>
#include <vector>

class State;
class SuccessorGenerator;
class TaskProxy;

namespace options {
class Options;
class OptionParser;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class Abstraction;

using CPFunction = std::function<CostPartitioning(
                                     const Abstractions &, const std::vector<int> &, const std::vector<int> &)>;

class CostPartitioningGenerator {
protected:
    const int max_orders;
    const double max_time;
    const bool diversify;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    // Members for random walks.
    std::unique_ptr<SuccessorGenerator> successor_generator;
    double average_operator_costs;
    std::unique_ptr<State> initial_state;
    int init_h;

    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs);

    virtual CostPartitioning get_next_cost_partitioning(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        const State &state,
        CPFunction cp_function) = 0;

    virtual bool has_next_cost_partitioning() const {
        return true;
    }

public:
    CostPartitioningGenerator(const options::Options &opts);
    virtual ~CostPartitioningGenerator();

    virtual CostPartitionings get_cost_partitionings(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        CPFunction cp_function);
};


extern CostPartitioning compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const std::vector<int> &order,
    const std::vector<int> &costs,
    bool debug = false);

extern void add_common_cp_generator_options_to_parser(
    options::OptionParser &parser);
}

#endif
