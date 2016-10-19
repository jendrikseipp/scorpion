#include "abstraction.h"

#include "../utils/collections.h"
#include "../utils/logging.h"

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
            assert(cost >= 0);
            int successor_distance = (cost == INF) ? INF : state_distance + cost;
            assert(successor_distance >= 0);
            if (distances[successor] > successor_distance) {
                distances[successor] = successor_distance;
                queue.push(successor_distance, successor);
            }
        }
    }
}

Abstraction::Abstraction(
    vector<vector<Transition>> &&backward_graph,
    vector<int> &&looping_operators,
    vector<int> &&goal_states,
    int num_operators)
    : backward_graph(move(backward_graph)),
      looping_operators(move(looping_operators)),
      goal_states(move(goal_states)),
      num_operators(num_operators),
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

    int num_states = backward_graph.size();
    for (int target = 0; target < num_states; ++target) {
        assert(utils::in_bounds(target, h_values));
        int target_h = h_values[target];
        assert(target_h != INF);

        for (const Transition &transition : backward_graph[target]) {
            int op_id = transition.op;
            int src = transition.state;
            assert(src != target);
            assert(utils::in_bounds(src, h_values));
            int src_h = h_values[src];
            assert(src_h != INF);

            const int needed = src_h - target_h;
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

void Abstraction::dump() const {
    cout << "State-changing transitions:" << endl;
    for (size_t state = 0; state < backward_graph.size(); ++state) {
        cout << "  " << state << " <- ";
        for (const Transition &transition : backward_graph[state]) {
            cout << "(" << transition.op << ", " << transition.state << ") ";
        }
        cout << endl;
    }
    cout << "Looping operators: " << looping_operators << endl;
    cout << "Goal states: " << goal_states << endl;
}
}

