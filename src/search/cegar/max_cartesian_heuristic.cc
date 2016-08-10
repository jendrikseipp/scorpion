#include "max_cartesian_heuristic.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "utils.h"

#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
static vector<int> get_default_order(int n) {
    vector<int> indices(n);
    iota(indices.begin(), indices.end(), 0);
    return indices;
}

static vector<vector<int>> compute_saturated_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &operator_costs) {
    assert(abstractions.size() == order.size());
    vector<int> remaining_costs = operator_costs;
    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
        abstraction.set_operator_costs(remaining_costs);
        h_values_by_abstraction[pos] = abstraction.get_h_values();
        reduce_costs(remaining_costs, abstraction.get_saturated_costs());
    }
    return h_values_by_abstraction;
}

vector<vector<vector<int>>> compute_saturated_cost_partitionings(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> operator_costs,
    int num_orders) {
    vector<int> indices = get_default_order(abstractions.size());
    vector<vector<vector<int>>> h_values_by_orders;
    for (int order = 0; order < num_orders; ++order) {
        g_rng()->shuffle(indices);
        h_values_by_orders.push_back(
            compute_saturated_cost_partitioning(
                abstractions, indices, operator_costs));
    }
    return h_values_by_orders;
}

MaxCartesianHeuristic::MaxCartesianHeuristic(
    const options::Options &opts,
    vector<unique_ptr<Abstraction>> &&abstractions,
    vector<vector<vector<int>>> &&h_values_by_orders)
    : Heuristic(opts),
      h_values_by_orders(move(h_values_by_orders)) {
    subtasks.reserve(abstractions.size());
    refinement_hierarchies.reserve(abstractions.size());
    for (auto &abstraction : abstractions) {
        subtasks.push_back(abstraction->get_task());
        refinement_hierarchies.push_back(abstraction->get_refinement_hierarchy());
    }
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

int MaxCartesianHeuristic::compute_max(
    const vector<int> &local_state_ids,
    const vector<vector<vector<int>>> &h_values_by_order) const {
    int max_h = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum(local_state_ids, h_values_by_abstraction);
        if (sum_h == INF) {
            return INF;
        }
        max_h = max(max_h, sum_h);
    }
    return max_h;
}

static vector<int> get_local_state_ids(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const State &state) {
    assert(subtasks.size() == refinement_hierarchies.size());
    vector<int> local_state_ids;
    local_state_ids.reserve(subtasks.size());
    for (size_t i = 0; i < subtasks.size(); ++i) {
        const AbstractTask &subtask = *subtasks[i];
        TaskProxy subtask_proxy(subtask);
        State local_state = subtask_proxy.convert_ancestor_state(state);
        local_state_ids.push_back(
            refinement_hierarchies[i]->get_node(local_state)->get_state_id());
    }
    return local_state_ids;
}

int MaxCartesianHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(
        subtasks, refinement_hierarchies, state);
    int max_h = compute_max(local_state_ids, h_values_by_orders);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int MaxCartesianHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}
}
