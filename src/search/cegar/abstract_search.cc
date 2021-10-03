#include "abstract_search.h"

#include "abstract_state.h"
#include "transition_system.h"
#include "utils.h"

#include "../utils/logging.h"
#include "../utils/memory.h"

#include <cassert>

using namespace std;

namespace cegar {
AbstractSearch::AbstractSearch(const vector<int> &costs)
    : operator_costs(costs) {
}

void AbstractSearch::reset(int num_states) {
    open_queue.clear();
    search_info.resize(num_states);
    goal_distances.resize(num_states, 0);
    for (AbstractSearchInfo &info : search_info) {
        info.reset();
    }
}

unique_ptr<Solution> AbstractSearch::extract_solution(int init_id, int goal_id) const {
    unique_ptr<Solution> solution = utils::make_unique_ptr<Solution>();
    int current_id = goal_id;
    while (current_id != init_id) {
        const Transition &prev = search_info[current_id].get_incoming_transition();
        solution->emplace_front(prev.op_id, current_id);
        assert(prev.target_id != current_id);
        current_id = prev.target_id;
    }
    return solution;
}

void AbstractSearch::update_goal_distances_of_states_on_trace(
    const Solution &solution, int init_id) {
    int goal_distance = 0;
    for (auto it = solution.rbegin(); it != solution.rend(); ++it) {
        const Transition &transition = *it;
        int current_state = transition.target_id;
        set_h_value(current_state, goal_distance);
        goal_distance += operator_costs[transition.op_id];
    }
    set_h_value(init_id, goal_distance);
}

unique_ptr<Solution> AbstractSearch::find_solution(
    const vector<Transitions> &transitions,
    int init_id,
    const Goals &goal_ids) {
    reset(transitions.size());
    search_info[init_id].decrease_g_value_to(0);
    open_queue.push(goal_distances[init_id], init_id);
    int goal_id = astar_search(transitions, goal_ids);
    open_queue.clear();
    bool has_found_solution = (goal_id != UNDEFINED);
    if (has_found_solution) {
        unique_ptr<Solution> solution = extract_solution(init_id, goal_id);
        return solution;
    } else {
        goal_distances[init_id] = INF;
    }
    return nullptr;
}

int AbstractSearch::astar_search(
    const vector<Transitions> &transitions, const Goals &goals) {
    while (!open_queue.empty()) {
        pair<int, int> top_pair = open_queue.pop();
        int old_f = top_pair.first;
        int state_id = top_pair.second;

        const int g = search_info[state_id].get_g_value();
        assert(g < INF);
        int new_f = g + goal_distances[state_id];
        assert(new_f <= old_f);
        if (new_f < old_f)
            continue;
        if (goals.count(state_id)) {
            return state_id;
        }
        assert(utils::in_bounds(state_id, transitions));
        for (const Transition &transition : transitions[state_id]) {
            int op_id = transition.op_id;
            int succ_id = transition.target_id;

            assert(utils::in_bounds(op_id, operator_costs));
            const int op_cost = operator_costs[op_id];
            assert(op_cost >= 0);
            int succ_g = (op_cost == INF) ? INF : g + op_cost;
            assert(succ_g >= 0);

            if (succ_g < search_info[succ_id].get_g_value()) {
                search_info[succ_id].decrease_g_value_to(succ_g);
                int succ_h = goal_distances[succ_id];
                if (succ_h == INF)
                    continue;
                int succ_f = succ_g + succ_h;
                open_queue.push(succ_f, succ_id);
                search_info[succ_id].set_incoming_transition(Transition(op_id, state_id));
            }
        }
    }
    return UNDEFINED;
}

int AbstractSearch::get_h_value(int state_id) const {
    assert(utils::in_bounds(state_id, goal_distances));
    return goal_distances[state_id];
}

void AbstractSearch::set_h_value(int state_id, int h) {
    assert(utils::in_bounds(state_id, goal_distances));
    goal_distances[state_id] = h;
}

void AbstractSearch::copy_h_value_to_children(int v, int v1, int v2) {
    int h = get_h_value(v);
    goal_distances.resize(goal_distances.size() + 1);
    set_h_value(v1, h);
    set_h_value(v2, h);
}


vector<int> compute_distances(
    const vector<Transitions> &transitions,
    const vector<int> &costs,
    const unordered_set<int> &start_ids) {
    vector<int> distances(transitions.size(), INF);
    priority_queues::AdaptiveQueue<int> open_queue;
    for (int goal_id : start_ids) {
        distances[goal_id] = 0;
        open_queue.push(0, goal_id);
    }
    while (!open_queue.empty()) {
        pair<int, int> top_pair = open_queue.pop();
        int old_g = top_pair.first;
        int state_id = top_pair.second;

        const int g = distances[state_id];
        assert(0 <= g && g < INF);
        assert(g <= old_g);
        if (g < old_g)
            continue;
        assert(utils::in_bounds(state_id, transitions));
        for (const Transition &transition : transitions[state_id]) {
            const int op_cost = costs[transition.op_id];
            assert(op_cost >= 0);
            int succ_g = (op_cost == INF) ? INF : g + op_cost;
            assert(succ_g >= 0);
            int succ_id = transition.target_id;
            if (succ_g < distances[succ_id]) {
                distances[succ_id] = succ_g;
                open_queue.push(succ_g, succ_id);
            }
        }
    }
    return distances;
}
}
