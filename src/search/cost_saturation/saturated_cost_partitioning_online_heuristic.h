#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H

#include "types.h"
#include "unsolvability_heuristic.h"

#include "../heuristic.h"

#include <deque>
#include <memory>
#include <vector>

namespace utils {
class Timer;
}

namespace cost_saturation {
class Diversifier;
class OrderGenerator;

class SaturatedCostPartitioningOnlineHeuristic : public Heuristic {
    const std::shared_ptr<OrderGenerator> order_generator;
    const CPFunction cp_function;
    Abstractions abstractions;
    CPHeuristics cp_heuristics;
    UnsolvabilityHeuristic unsolvability_heuristic;
    const int interval;
    const double max_time;
    const bool diversify;
    const int num_samples;

    const std::vector<int> costs;

    bool improve_heuristic;

    std::vector<int> fact_id_offsets;
    std::vector<bool> seen_facts;
    std::vector<std::vector<bool>> seen_fact_pairs;

    std::unique_ptr<Diversifier> diversifier;

    utils::HashSet<Order> seen_orders;
    std::unique_ptr<utils::Timer> compute_heuristic_timer;
    std::unique_ptr<utils::Timer> convert_global_state_timer;
    std::unique_ptr<utils::Timer> improve_heuristic_timer;
    std::unique_ptr<utils::Timer> compute_orders_timer;
    std::unique_ptr<utils::Timer> get_abstract_state_ids_timer;
    std::unique_ptr<utils::Timer> unsolvability_heuristic_timer;
    std::unique_ptr<utils::Timer> compute_max_h_timer;
    std::unique_ptr<utils::Timer> compute_novelty_timer;
    std::unique_ptr<utils::Timer> compute_scp_timer;
    std::unique_ptr<utils::Timer> compute_h_timer;
    std::unique_ptr<utils::Timer> diversification_timer;
    int num_duplicate_orders;
    int num_evaluated_states;
    int num_scps_computed;

    // For statistics.
    mutable std::vector<int> num_best_order;

    void print_statistics() const;
    void setup_diversifier(utils::RandomNumberGenerator &rng);
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
        const GlobalState &state) override;
};
}

#endif
