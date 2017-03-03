#ifndef CEGAR_SCP_OPTIMIZER_H
#define CEGAR_SCP_OPTIMIZER_H

#include <memory>
#include <vector>

class AbstractTask;
class State;

namespace utils {
class CountdownTimer;
class RandomNumberGenerator;
class Timer;
}

namespace cegar {
class Abstraction;
class RefinementHierarchy;

class SCPOptimizer {
    const std::vector<std::unique_ptr<Abstraction>> abstractions;
    const std::vector<std::shared_ptr<RefinementHierarchy>> refinement_hierarchies;
    const std::vector<int> operator_costs;
    static const bool debug = false;
    std::shared_ptr<utils::RandomNumberGenerator> rng;

    mutable std::vector<std::vector<int>> incumbent_scp;
    mutable int evaluations;

    int evaluate(
        const std::vector<std::vector<int>> &h_values_by_abstraction,
        const std::vector<std::vector<int>> &local_state_ids_by_state,
        const std::vector<int> &portfolio_h_values,
        std::vector<int> &portfolio_h_values_improvement) const;

    bool search_improving_successor(
        const utils::CountdownTimer &timer,
        const std::vector<std::vector<int>> &local_state_ids_by_state,
        std::vector<int> &incumbent_order,
        int &incumbent_total_h_value,
        const std::vector<int> &portfolio_h_values,
        std::vector<int> &portfolio_h_values_improvement) const;

public:
    std::unique_ptr<utils::Timer> scp_computation_timer;
    std::unique_ptr<utils::Timer> order_evaluation_timer;

    SCPOptimizer(
        std::vector<std::unique_ptr<Abstraction>> &&abstractions,
        const std::vector<std::shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
        const std::vector<int> &operator_costs,
        const std::shared_ptr<utils::RandomNumberGenerator> &rng);

    std::pair<std::vector<std::vector<int>>, std::pair<int, int>> find_cost_partitioning(
        const std::vector<std::vector<int>> &local_state_ids_by_state,
        double max_time,
        bool shuffle,
        bool reverse,
        const std::vector<int> &portfolio_h_values,
        std::vector<int> &portfolio_h_values_improvement) const;
};

extern std::vector<int> get_default_order(int n);

extern std::vector<int> get_local_state_ids(
    const std::vector<std::shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const State &state);

extern std::vector<std::vector<int>> compute_saturated_cost_partitioning(
    const std::vector<std::unique_ptr<Abstraction>> &abstractions,
    const std::vector<int> &order,
    const std::vector<int> &operator_costs);

extern int compute_sum_h(
    const std::vector<int> &local_state_ids,
    const std::vector<std::vector<int>> &h_values_by_abstraction);

extern int compute_max_h(
    const std::vector<int> &local_state_ids,
    const std::vector<std::vector<std::vector<int>>> &h_values_by_order);
}

#endif
