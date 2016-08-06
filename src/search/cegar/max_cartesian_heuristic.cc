#include "max_cartesian_heuristic.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "utils.h"

#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
MaxCartesianHeuristic::MaxCartesianHeuristic(
    const options::Options &opts,
    std::vector<std::unique_ptr<Abstraction>> &&abstractions,
    int num_orders)
    : Heuristic(opts) {
    subtasks.reserve(abstractions.size());
    refinement_hierarchies.reserve(abstractions.size());
    for (auto &abstraction : abstractions) {
        subtasks.push_back(abstraction->get_task());
        refinement_hierarchies.push_back(abstraction->get_refinement_hierarchy());
    }

    vector<int> indices(abstractions.size());
    iota(indices.begin(), indices.end(), 0);

    for (int order = 0; order < num_orders; ++order) {
        g_rng()->shuffle(indices);
        h_values_by_orders.push_back(
            compute_saturated_cost_partitioning(abstractions, indices));
    }
}

vector<vector<int>> MaxCartesianHeuristic::compute_saturated_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order) const {
    assert(abstractions.size() == order.size());

    vector<int> remaining_costs;
    remaining_costs.reserve(task_proxy.get_operators().size());
    for (OperatorProxy op : task_proxy.get_operators()) {
        remaining_costs.push_back(op.get_cost());
    }

    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        const unique_ptr<Abstraction> &abstraction = abstractions[pos];
        abstraction->set_operator_costs(remaining_costs);
        h_values_by_abstraction[pos] = abstraction->get_h_values();
        reduce_costs(remaining_costs, abstraction->get_saturated_costs());
    }
    return h_values_by_abstraction;
}

int MaxCartesianHeuristic::compute_sum(
    const vector<int> &local_state_ids,
    const vector<vector<int>> &h_values_by_abstraction) const {
    int sum_h = 0;
    assert(local_state_ids.size() == h_values_by_abstraction.size());
    for (size_t i = 0; i < local_state_ids.size(); ++i) {
        int state_id = local_state_ids[i];
        const vector<int> &h_values = h_values_by_abstraction[i];
        assert(utils::in_bounds(state_id, h_values));
        int value = h_values[state_id];
        assert(value >= 0);
        if (value == INF)
            return INF;
        sum_h += value;
    }
    assert(sum_h >= 0);
    return sum_h;
}

int MaxCartesianHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids;
    local_state_ids.reserve(subtasks.size());
    for (size_t i = 0; i < subtasks.size(); ++i) {
        const AbstractTask &subtask = *subtasks[i];
        TaskProxy subtask_proxy(subtask);
        State local_state = subtask_proxy.convert_ancestor_state(state);
        local_state_ids.push_back(
            refinement_hierarchies[i]->get_node(local_state)->get_state_id());
    }
    int max_h = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_orders) {
        int sum_h = compute_sum(local_state_ids, h_values_by_abstraction);
        if (sum_h == INF) {
            return DEAD_END;
        }
        max_h = max(max_h, sum_h);
    }
    return max_h;
}

int MaxCartesianHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}
}
