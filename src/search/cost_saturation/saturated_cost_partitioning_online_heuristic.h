#ifndef COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H
#define COST_SATURATION_SATURATED_COST_PARTITIONING_ONLINE_HEURISTIC_H

#include "saturated_cost_partitioning_heuristic.h"
#include "types.h"

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

struct Sample {
    std::vector<int> abstract_state_ids;
    int max_h;

    Sample(std::vector<int> &&abstract_state_ids, int max_h)
        : abstract_state_ids(move(abstract_state_ids)),
          max_h(max_h) {
    }
};

class OnlineDiversifier {
    utils::HashMap<StateID, Sample> samples;
public:
    bool add_cp_if_diverse(const CostPartitioningHeuristic &cp_heuristic);
    void add_sample(StateID state_id, std::vector<int> &&abstract_state_ids, int max_h);
    void remove_sample(StateID state_id);
};

class SaturatedCostPartitioningOnlineHeuristic : public Heuristic {
    const std::shared_ptr<OrderGenerator> order_generator;
    const Saturator saturator;
    const CPFunction cp_function;
    Abstractions abstractions;
    AbstractionFunctions abstraction_functions;
    CPHeuristics cp_heuristics;
    const int interval;
    const double max_time;
    const int max_size_kb;
    const bool use_offline_samples;
    const int num_samples;
    const bool sample_from_generated_states;
    const bool use_evaluated_state_as_sample;
    const bool debug;

    const std::vector<int> costs;

    bool improve_heuristic;
    PerStateInformation<int> num_orders_used_for_state;

    std::vector<int> fact_id_offsets;
    std::vector<bool> seen_facts;
    std::vector<std::vector<bool>> seen_fact_pairs;

    bool should_compute_scp_for_bellman;

    std::unique_ptr<Diversifier> diversifier;
    std::unique_ptr<OnlineDiversifier> online_diversifier;

    std::unique_ptr<utils::Timer> improve_heuristic_timer;
    int size_kb;
    int num_evaluated_states;
    int num_scps_computed;

    // For statistics.
    mutable std::vector<int> num_best_order;

    void print_diversification_statistics() const;
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
        CPHeuristics &&cp_heuristics);
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

    virtual bool is_cached_estimate_dirty(const GlobalState &state) const override;

    void compute_scp_and_store_if_diverse(const GlobalState &state);

    int get_interval() const {
        return interval;
    }

    bool is_improve_mode_on() const {
        return improve_heuristic;
    }
};
}

#endif
