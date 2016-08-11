#ifndef CEGAR_SCP_OPTIMIZER_H
#define CEGAR_SCP_OPTIMIZER_H

#include <memory>
#include <vector>

class AbstractTask;
class State;

namespace cegar {
class Abstraction;
class RefinementHierarchy;

class SCPOptimizer {
    const std::vector<std::unique_ptr<Abstraction>> &&abstractions;
    const std::vector<std::shared_ptr<AbstractTask>> subtasks;
    const std::vector<std::shared_ptr<RefinementHierarchy>> refinement_hierarchies;
    const std::vector<std::vector<int>> local_state_ids_by_state;
    const std::vector<int> operator_costs;

    std::vector<int> incumbent_order;
    int incumbent_total_h_value;

    int evaluate(const std::vector<int> &order) const;

    bool search_improving_successor();

public:
    SCPOptimizer(
        std::vector<std::unique_ptr<Abstraction>> &&abstractions,
        std::vector<std::shared_ptr<AbstractTask>> &&subtasks,
        std::vector<std::shared_ptr<RefinementHierarchy>> &&refinement_hierarchies,
        const std::vector<int> &operator_costs,
        const std::vector<State> &states);

    std::vector<int> extract_order();
};

extern std::vector<int> get_default_order(int n);

extern std::vector<int> get_local_state_ids(
    const std::vector<std::shared_ptr<AbstractTask>> &subtasks,
    const std::vector<std::shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const State &state);

extern std::vector<std::vector<std::vector<int>>>
compute_saturated_cost_partitionings(
    const std::vector<std::unique_ptr<Abstraction>> &abstractions,
    const std::vector<int> operator_costs,
    int num_orders);

extern std::vector<std::vector<int>> compute_saturated_cost_partitioning(
    const std::vector<std::unique_ptr<Abstraction>> &abstractions,
    const std::vector<int> &order,
    const std::vector<int> &operator_costs);

extern int compute_sum_h(
    const std::vector<int> &local_state_ids,
    const std::vector<std::vector<int>> &h_values_by_abstraction);
}

#endif
