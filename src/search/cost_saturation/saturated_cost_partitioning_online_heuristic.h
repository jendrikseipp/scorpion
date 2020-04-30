#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H

#include "saturated_cost_partitioning_heuristic.h"
#include "types.h"
#include "unsolvability_heuristic.h"

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
    CPHeuristics cp_heuristics;
    UnsolvabilityHeuristic unsolvability_heuristic;
    const int interval;
    const double max_time;
    const int max_size_kb;
    const bool store_diverse_orders;

    const std::vector<int> costs;

    bool improve_heuristic;

    std::vector<int> fact_id_offsets;
    std::vector<bool> seen_facts;
    std::vector<std::vector<bool>> seen_fact_pairs;

    std::unique_ptr<utils::Timer> timer;
    int size_kb;
    int num_evaluated_states;
    int num_scps_computed;

    // For statistics.
    mutable std::vector<int> num_best_order;

    void print_diversification_statistics() const;
    void print_statistics() const;
    int get_fact_id(int var, int value) const;
    bool visit_fact_pair(int fact_id1, int fact_id2);
    bool is_novel(OperatorID op_id, const GlobalState &state);
    bool should_compute_scp(const GlobalState &global_state);

protected:
    virtual int compute_heuristic(const GlobalState &state) override;

public:
    SaturatedCostPartitioningOnlineHeuristic(
        const options::Options &opts,
        Abstractions &&abstractions,
        CPHeuristics &&cp_heuristics,
        UnsolvabilityHeuristic &&unsolvability_heuristic);
    virtual ~SaturatedCostPartitioningOnlineHeuristic() override;

    virtual void get_path_dependent_evaluators(
        std::set<Evaluator *> &evals) override {
        evals.insert(this);
    }

    virtual void notify_initial_state(const GlobalState &initial_state) override;

    virtual void notify_state_transition(
        const GlobalState &,
        OperatorID op_id,
        const GlobalState &global_state) override;
};
}

#endif
