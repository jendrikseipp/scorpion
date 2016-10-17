#include "abstraction.h"

#include "../utils/collections.h"

using namespace std;

namespace cost_saturation {

static void dijkstra_search(
    const vector<vector<Transition>> &graph,
    AdaptiveQueue<int> &queue,
    vector<int> &distances,
    const vector<int> &costs) {
    while (!queue.empty()) {
        pair<int, int> top_pair = queue.pop();
        int distance = top_pair.first;
        int state = top_pair.second;
        int state_distance = distances[state];
        assert(state_distance <= distance);
        if (state_distance < distance) {
            continue;
        }
        for (const Transition &transition : graph[state]) {
            int successor = transition.state;
            int op = transition.op;
            assert(utils::in_bounds(op, costs));
            int cost = costs[op];
            int successor_cost = state_distance + cost;
            if (distances[successor] > successor_cost) {
                distances[successor] = successor_cost;
                queue.push(successor_cost, successor);
            }
        }
    }
}

Abstraction::Abstraction(int num_operators)
    : num_operators(num_operators),
      use_general_costs(true) {
}

std::vector<int> Abstraction::compute_h_values(
    const std::vector<int> &costs) const {
    vector<int> goal_distances(backward_graph.size(), INF);
    queue.clear();
    for (int goal_state : goal_states) {
        goal_distances[goal_state] = 0;
        queue.push(0, goal_state);
    }
    dijkstra_search(backward_graph, queue, goal_distances, costs);
    return goal_distances;
}

vector<int> Abstraction::compute_saturated_costs(
    const vector<int> &h_values) const {
    const int min_cost = use_general_costs ? -INF : 0;

    vector<int> saturated_costs(num_operators, min_cost);

    /* To prevent negative cost cycles we ensure that all operators
       inducing self-loops have non-negative costs. */
    if (use_general_costs) {
        for (int op_id : looping_operators) {
            saturated_costs[op_id] = 0;
        }
    }

    for (size_t state = 0; state < backward_graph.size(); ++state) {
        assert(utils::in_bounds(state, h_values));
        int h = h_values[state];
        assert(h != INF);

        for (const Transition &transition : backward_graph[state]) {
            int op_id = transition.op;
            int successor = transition.state;
            assert(utils::in_bounds(successor, h_values));
            int succ_h = h_values[successor];
            assert(succ_h != INF);

            const int needed = h - succ_h;
            assert(needed >= 0);
            saturated_costs[op_id] = max(saturated_costs[op_id], needed);
        }
    }
    return saturated_costs;
}

pair<vector<int>, vector<int>> Abstraction::compute_goal_distances_and_saturated_costs(
    const vector<int> &costs) {
    vector<int> h_values = compute_h_values(costs);
    vector<int> saturated_costs = compute_saturated_costs(h_values);
    return make_pair(move(h_values), move(saturated_costs));
}
}

