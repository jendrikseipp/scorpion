#include "max_cartesian_heuristic.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "scp_optimizer.h"
#include "utils.h"

#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
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

int MaxCartesianHeuristic::compute_max(
    const vector<int> &local_state_ids,
    const vector<vector<vector<int>>> &h_values_by_order) const {
    int max_h = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        if (sum_h == INF) {
            return INF;
        }
        max_h = max(max_h, sum_h);
    }
    return max_h;
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
