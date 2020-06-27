#include "shortest_paths.h"

#include "abstract_search.h"
#include "abstraction.h"
#include "transition_system.h"
#include "utils.h"

#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include <cassert>
#include <map>

using namespace std;

namespace cegar {
const Cost ShortestPaths::INF_COSTS = numeric_limits<Cost>::max();

ShortestPaths::ShortestPaths(
    const vector<int> &costs, const utils::CountdownTimer &timer, bool debug)
    : timer(timer),
      debug(debug),
      task_has_zero_costs(any_of(costs.begin(), costs.end(), [](int c) {return c == 0;})) {
    /*
      The code below requires that all operators have positive cost. Negative
      operators are of course tricky, but 0-cost operators are somewhat tricky,
      too. In particular, given perfect g and h values, we want to know which
      operators make progress towards the goal, and this is easy to do if all
      operator costs are positive (then *all* operators that lead to a state
      with the same f value as the current one make progress towards the goal,
      in the sense that following those operators will necessarily take us to
      the goal on a path with strictly decreasing h values), but not if they
      may be 0 (consider the case where all operators cost 0: then the f*
      values of all alive states are 0, so they give us no guidance towards the
      goal).

      If the assumption of no 0-cost operators is violated, the easiest way to
      address this is to replace all 0-cost operators with operators of cost
      epsilon, where epsilon > 0 is small enough that "rounding down" epsilons
      along a shortest path always results in the correct original cost. With
      original integer costs, picking epsilon <= 1/N for a state space with N
      states is sufficient for this. In our actual implementation, we should
      not use floating-point numbers, and if we stick with 32-bit integers for
      path costs, we can run into range issues. The most obvious solution is to
      use 64-bit integers, scaling all original operator costs by 2^32 and
      using epsilon = 1.
    */
    operator_costs.reserve(costs.size());
    for (int cost : costs) {
        operator_costs.push_back(convert_to_64_bit_cost(cost));
    }
}

Cost ShortestPaths::add_costs(Cost a, Cost b) {
    return (a == INF_COSTS || b == INF_COSTS) ? INF_COSTS : a + b;
}

int ShortestPaths::convert_to_32_bit_cost(Cost cost) const {
    if (cost == INF_COSTS) {
        return INF;
    } else if (task_has_zero_costs) {
        return static_cast<int>(cost >> 32);
    } else {
        return cost;
    }
}

Cost ShortestPaths::convert_to_64_bit_cost(int cost) const {
    assert(cost >= 0);
    if (cost == INF) {
        return INF_COSTS;
    } else if (task_has_zero_costs) {
        if (cost == 0) {
            return 1;
        } else {
            return static_cast<uint64_t>(cost) << 32;
        }
    } else {
        return cost;
    }
}

unique_ptr<Solution> ShortestPaths::extract_solution_from_shortest_path_tree(
    int init_id, const Goals &goals) {
    // h* = \infty iff goal is unreachable from this state.
    if (states[init_id].goal_distance == INF_COSTS)
        return nullptr;

    int current_state = init_id;
    unique_ptr<Solution> solution = utils::make_unique_ptr<Solution>();
    assert(!goals.count(current_state));
    while (!goals.count(current_state)) {
        const Transition &t = states[current_state].parent;
        assert(t.op_id != UNDEFINED);
        assert(t.target_id != UNDEFINED);
        assert(t.target_id != current_state);
        assert(states[t.target_id].goal_distance <= states[current_state].goal_distance);
        solution->push_back(t);
        current_state = t.target_id;
    }
    return solution;
}

vector<int> ShortestPaths::get_goal_distances() const {
    vector<int> distances;
    distances.reserve(states.size());
    for (const StateInfo &state : states) {
        distances.push_back(convert_to_32_bit_cost(state.goal_distance));
    }
    return distances;
}

void ShortestPaths::set_shortest_path(int state, const Transition &new_parent) {
    int op_id = new_parent.op_id;
    if (states[state].parent != new_parent) {
        Transition old_parent = states[state].parent;
        if (old_parent.is_defined()) {
            Transition old_child(old_parent.op_id, state);
            ShortestPathChildren &old_children = states[old_parent.target_id].children;
            auto it = find(old_children.begin(), old_children.end(), old_child);
            assert(it != old_children.end());
            utils::swap_and_pop_from_vector(old_children, it - old_children.begin());
        }
        states[state].parent = new_parent;
        if (new_parent.is_defined()) {
            states[new_parent.target_id].children.emplace_back(op_id, state);
        }
    }
}

void ShortestPaths::mark_dirty(int state) {
    if (debug) {
        cout << "Mark " << state << " as dirty" << endl;
    }
    states[state].dirty = true;
    // Previous shortest path is invalid now.
    set_shortest_path(state, Transition());
    assert(!count(dirty_states.begin(), dirty_states.end(), state));
    dirty_states.push_back(state);
}

void ShortestPaths::dijkstra_from_orphans(
    const Abstraction &abstraction, int v, int v1, int v2) {
    /*
      Assumption: all h-values correspond to the perfect heuristic for the
      state space before the split.

      orphans holds the newly computed reverse g-values (i.e., h-values) for
      orphaned states, and SETTLED for settled states. A state is orphaned if
      at least one of its possible shortest-path successors is orphaned,
      starting with s_1. We start by assuming g=\infty for all orphaned states.
    */
    int num_states = abstraction.get_num_states();
    states.resize(num_states);
    dirty_states.clear();

    if (debug) {
        cout << "Split " << v << " into " << v1 << " and " << v2 << endl;
    }

#ifndef NDEBUG
    Transition old_arc = states[v].parent;
    Transitions v1_out = abstraction.get_outgoing_transitions(v1);
    bool v1_settled = any_of(v1_out.begin(), v1_out.end(),
                             [&old_arc](const Transition &t) {
                                 return t == old_arc;
                             });
    Transitions v2_out = abstraction.get_outgoing_transitions(v2);
    bool v2_settled = any_of(v2_out.begin(), v2_out.end(),
                             [&old_arc](const Transition &t) {
                                 return t == old_arc;
                             });
    assert(v1_settled ^ v2_settled); // Otherwise, there would be no progress.
    assert(v2_settled); // Implementation detail which we use below.
#endif

    // Copy h value from split state. h(v1) will be updated if necessary.
    states[v1].goal_distance = states[v2].goal_distance = states[v].goal_distance;

    /* Due to the way we select splits, the old shortest path from v1 is
       invalid now, but the path from v2 is still valid. We don't explicitly
       invalidate shortest_path[v1] since v and v1 are the same ID. */
    // TODO: set SP for v1 explicitly? Or at least assert that v==v1?
    set_shortest_path(v2, states[v].parent);

    /* Update shortest path transitions to split state. The SPT transition to v1
       will be updated again if v1 is dirty. We therefore prefer reconnecting
       states to v2 instead of v1. */
    // We need to copy the vector since we reuse the index v.
    ShortestPathChildren old_children = states[v].children;
    for (const Transition &old_child : old_children) {
        int u = old_child.target_id;
        int old_cost = convert_to_32_bit_cost(operator_costs[old_child.op_id]);
        int op_id = abstraction.get_operator_between_states(u, v2, old_cost);
        Transition new_parent = (op_id == UNDEFINED)
            ? Transition(old_child.op_id, v1)
            : Transition(op_id, v2);
        set_shortest_path(u, new_parent);
    }

    /*
      Instead of just recursively inserting all orphans, we first push them
      into a candidate queue that is sorted by (old, possibly too low)
      h-values. Then, we try to reconnect them to a non-orphaned state at
      no additional cost. Only if that fails, we flag the candidate as
      orphaned and push its SPT-children (who have strictly larger h-values
      due to no 0-cost operators) into the candidate queue.
    */
    assert(candidate_queue.empty());
    assert(all_of(states.begin(), states.end(), [](const StateInfo &s) {
                      return !s.dirty || s.goal_distance == INF_COSTS;
                  }));

    states[v1].dirty_candidate = true;
    candidate_queue.push(states[v1].goal_distance, v1);

    while (!candidate_queue.empty()) {
        int state = candidate_queue.pop().second;
        if (debug) {
            cout << "Try to reconnect " << state
                 << " with h=" << states[state].goal_distance << endl;
        }
        assert(states[state].dirty_candidate);
        assert(states[state].goal_distance != INF_COSTS);
        assert(!states[state].dirty);
        bool reconnected = false;
        // Try to reconnect to settled, solvable state.
        abstraction.for_each_outgoing_transition(
            state, [&](const Transition &t) {
                int succ = t.target_id;
                int op_id = t.op_id;
                if (!states[succ].dirty &&
                    add_costs(states[succ].goal_distance, operator_costs[op_id])
                    == states[state].goal_distance) {
                    if (debug) {
                        cout << "Reconnect " << state << " to " << succ << " via "
                             << op_id << " with cost " << operator_costs[op_id]
                             << " (" << convert_to_32_bit_cost(operator_costs[op_id])
                             << ")" << endl;
                    }
                    assert(states[state].goal_distance != INF_COSTS);
                    assert(states[succ].goal_distance != INF_COSTS);
                    assert(operator_costs[op_id] != INF_COSTS);
                    set_shortest_path(state, Transition(op_id, succ));
                    reconnected = true;
                }
                return reconnected;
            });
        if (!reconnected) {
            mark_dirty(state);
            for (const Transition &t : states[state].children) {
                int prev = t.target_id;
                assert(states[prev].parent.target_id == state);
                if (!states[prev].dirty_candidate && !states[prev].dirty) {
                    if (debug) {
                        cout << "Add " << prev << " to candidate queue" << endl;
                    }
                    states[prev].dirty_candidate = true;
                    candidate_queue.push(states[prev].goal_distance, prev);
                }
            }
        }
        states[state].dirty_candidate = false;

        if (timer.is_expired()) {
            // All goal distances are always lower bounds, so we can abort at any time.
            cout << "Timer expired --> abort incremental search" << endl;
            return;
        }
    }

#ifndef NDEBUG
    for (int i = 0; i < num_states; ++i) {
        if (states[i].dirty && states[i].goal_distance != INF_COSTS) {
            assert(count(dirty_states.begin(), dirty_states.end(), i) == 1);
        }
    }
    // Goal states must never be dirty.
    for (int goal : abstraction.get_goals()) {
        assert(!count(dirty_states.begin(), dirty_states.end(), goal));
    }
#endif

    /*
      Perform a Dijkstra-style exploration to recompute all h values as
      follows. The "initial state" of the search is a virtual state that
      represents all settled states. It is expanded first, starting with a cost
      of 0. Its outgoing arcs are all arcs (in the backward graph) that go from
      a settled state s to a dirty state s' with operator o, and the cost of
      the transition is h(s) + cost(o). (Note that h(s) for settled states is
      known.) After this initialization, proceed with a normal Dijkstra search,
      but only consider arcs that lead from dirty to dirty states.
    */
    int num_orphans = 0;
    open_queue.clear();
    for (int state : dirty_states) {
        assert(states[state].dirty);
        Cost min_dist = INF_COSTS;
        for (const Transition &t : abstraction.get_outgoing_transitions(state)) {
            int succ = t.target_id;
            int op_id = t.op_id;
            if (!states[succ].dirty) {
                Cost succ_dist = states[succ].goal_distance;
                Cost cost = operator_costs[op_id];
                Cost new_dist = add_costs(cost, succ_dist);
                if (new_dist < min_dist) {
                    min_dist = new_dist;
                    set_shortest_path(state, Transition(op_id, succ));
                }
            }
        }
        states[state].goal_distance = min_dist;
        if (min_dist != INF_COSTS) {
            open_queue.push(min_dist, state);
            ++num_orphans;
        }
    }

    while (!open_queue.empty()) {
        pair<Cost, int> top_pair = open_queue.pop();
        const Cost g = top_pair.first;
        const int state = top_pair.second;
        assert(count(dirty_states.begin(), dirty_states.end(), state) == 1);
        if (g > states[state].goal_distance)
            continue;
        assert(g == states[state].goal_distance);
        assert(g != INF_COSTS);
        assert(states[state].dirty);
        states[state].dirty = false;
        for (const Transition &t : abstraction.get_incoming_transitions(state)) {
            int succ = t.target_id;
            int op_id = t.op_id;
            Cost cost = operator_costs[op_id];
            Cost succ_g = add_costs(cost, g);

            if (states[succ].dirty && succ_g < states[succ].goal_distance) {
                assert(count(dirty_states.begin(), dirty_states.end(), succ) == 1);
                states[succ].goal_distance = succ_g;
                set_shortest_path(succ, Transition(op_id, state));
                open_queue.push(succ_g, succ);
            }
        }
    }
}

void ShortestPaths::full_dijkstra(
    const Abstraction &abstraction,
    const unordered_set<int> &goals) {
    open_queue.clear();
    int num_states = abstraction.get_num_states();
    states.resize(num_states);
    for (StateInfo &state : states) {
        state.goal_distance = INF_COSTS;
    }
    for (int goal : goals) {
        Cost dist = 0;
        states[goal].goal_distance = dist;
        set_shortest_path(goal, Transition());
        open_queue.push(dist, goal);
    }
    while (!open_queue.empty()) {
        pair<Cost, int> top_pair = open_queue.pop();
        Cost old_g = top_pair.first;
        int state_id = top_pair.second;

        Cost g = states[state_id].goal_distance;
        assert(g < INF_COSTS);
        assert(g <= old_g);
        if (g < old_g)
            continue;
        for (const Transition &t : abstraction.get_incoming_transitions(state_id)) {
            int succ_id = t.target_id;
            int op_id = t.op_id;
            Cost op_cost = operator_costs[op_id];
            Cost succ_g = add_costs(g, op_cost);
            if (succ_g < states[succ_id].goal_distance) {
                states[succ_id].goal_distance = succ_g;
                set_shortest_path(succ_id, Transition(op_id, state_id));
                open_queue.push(succ_g, succ_id);
            }
        }
    }
}

bool ShortestPaths::test_distances(
    const Abstraction &abstraction,
    const unordered_set<int> &goals) {
    assert(all_of(states.begin(), states.end(), [](const StateInfo &s) {
                      return !s.dirty || s.goal_distance == INF_COSTS;
                  }));
    int num_states = abstraction.get_num_states();

    vector<int> costs;
    costs.reserve(operator_costs.size());
    for (Cost cost : operator_costs) {
        costs.push_back(convert_to_32_bit_cost(cost));
    }

    //int init_state = 0;
    vector<int> init_distances(num_states, 0); // Don't compute reachability info.
    // = compute_distances(out, costs, {init_state});

    for (int i = 0; i < num_states; ++i) {
        if (debug) {
            cout << "Test state " << i << endl;
        }
        if (states[i].goal_distance != INF_COSTS &&
            init_distances[i] != INF &&
            !goals.count(i)) {
            const Transition &t = states[i].parent;
            if (debug) {
                cout << "SP: " << t << endl;
            }
            assert(t.is_defined());
            Transitions out = abstraction.get_outgoing_transitions(i);
            if (debug) {
                cout << "Outgoing transitions: " << out << endl;
            }
            assert(count(out.begin(), out.end(), t) == 1);
            assert(states[i].goal_distance ==
                   add_costs(operator_costs[t.op_id], states[t.target_id].goal_distance));
        }
    }

    vector<int> goal_distances_32_bit = compute_goal_distances(abstraction, costs, goals);
    vector<int> goal_distances_32_bit_rounded_down = get_goal_distances();

    for (int i = 0; i < num_states; ++i) {
        if (goal_distances_32_bit_rounded_down[i] != goal_distances_32_bit[i] &&
            init_distances[i] != INF) {
            cout << "32-bit INF: " << INF << endl;
            cout << "64-bit 0: " << convert_to_64_bit_cost(0) << endl;
            cout << "64-bit 1: " << convert_to_64_bit_cost(1) << endl;
            cout << "64-bit INF: " << INF_COSTS << endl;
            cout << "32-bit rounded:   " << goal_distances_32_bit_rounded_down << endl;
            cout << "32-bit distances: " << goal_distances_32_bit << endl;
            ABORT("Distances are wrong.");
        }
    }

    return true;
}

void ShortestPaths::print_statistics() const {
    if (false) {
        map<int, int> children_counts;
        for (const StateInfo &state : states) {
            ++children_counts[state.children.size()];
        }
        for (auto &pair : children_counts) {
            cout << pair.first << " children: " << pair.second << endl;
        }
    }
    cout << "Shortest path tree estimated memory usage: "
         << estimate_memory_usage_in_bytes(states) / 1024 << " KB" << endl;
    uint64_t mem_usage = 0;
    for (const StateInfo &state : states) {
        mem_usage += estimate_memory_usage_in_bytes(state.children) - sizeof(state.children);
    }
    cout << "Shortest path children estimated memory usage: "
         << mem_usage / 1024 << " KB" << endl;
}
}
