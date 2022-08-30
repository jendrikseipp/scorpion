#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace utils {
class Timer;
}

namespace cost_saturation {
class OrderGenerator;

class SaturatedCostPartitioningOnlineHeuristic : public Heuristic {
    const std::shared_ptr<OrderGenerator> order_generator;
    const Saturator saturator;
    const CPFunction cp_function;
    Abstractions abstractions;
    AbstractionFunctions abstraction_functions;
    std::unique_ptr<DeadEnds> dead_ends;
    CPHeuristics cp_heuristics;
    const int interval;
    const double max_time;
    const int max_size_kb;
    const bool debug;

    const std::vector<int> costs;
    bool improve_heuristic;
    std::unique_ptr<utils::Timer> improve_heuristic_timer;
    std::unique_ptr<utils::Timer> select_state_timer;
    int size_kb;
    int num_evaluated_states;
    int num_scps_computed;

    void print_intermediate_statistics() const;
    void print_final_statistics() const;

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    SaturatedCostPartitioningOnlineHeuristic(
        const options::Options &opts,
        Abstractions &&abstractions,
        std::unique_ptr<DeadEnds> &&dead_ends);
    virtual ~SaturatedCostPartitioningOnlineHeuristic() override;
};
}

#endif
