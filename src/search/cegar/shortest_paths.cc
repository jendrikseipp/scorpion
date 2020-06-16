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
const Cost ShortestPaths::DIRTY = numeric_limits<Cost>::max() - 1;

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
    assert(a != DIRTY && b != DIRTY);
    return (a == INF_COSTS || b == INF_COSTS) ? INF_COSTS : a + b;
}

int ShortestPaths::convert_to_32_bit_cost(Cost cost) const {
    assert(cost != DIRTY);
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
    if (goal_distances[init_id] == INF_COSTS)
        return nullptr;

    int current_state = init_id;
    unique_ptr<Solution> solution = utils::make_unique_ptr<Solution>();
    assert(!goals.count(current_state));
    while (!goals.count(current_state)) {
        assert(utils::in_bounds(current_state, shortest_path));
        const Transition &t = shortest_path[current_state];
        assert(t.op_id != UNDEFINED);
        assert(t.target_id != UNDEFINED);
        assert(t.target_id != current_state);
        assert(goal_distances[t.target_id] <= goal_distances[current_state]);
        solution->push_back(t);
        current_state = t.target_id;
    }
    return solution;
}

vector<int> ShortestPaths::get_goal_distances() const {
    vector<int> distances;
    distances.reserve(goal_distances.size());
    for (Cost d : goal_distances) {
        distances.push_back(convert_to_32_bit_cost(d));
    }
    return distances;
}

void ShortestPaths::set_shortest_path(int state, const Transition &new_parent) {
    int op_id = new_parent.op_id;
    if (shortest_path[state] != new_parent) {
        Transition old_parent = shortest_path[state];
        if (old_parent.is_defined()) {
            Transition old_child(old_parent.op_id, state);
            Children &old_children = children[old_parent.target_id];
            auto it = find(old_children.begin(), old_children.end(), old_child);
            assert(it != old_children.end());
            utils::swap_and_pop_from_vector(old_children, it - old_children.begin());
        }
        shortest_path[state] = new_parent;
        if (new_parent.is_defined()) {
            children[new_parent.target_id].emplace_back(op_id, state);
        }
    }
}

void ShortestPaths::mark_dirty(int state) {
    if (debug) {
        cout << "Mark " << state << " as dirty" << endl;
    }
    goal_distances[state] = DIRTY;
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
    shortest_path.resize(num_states);
    children.resize(num_states);
    goal_distances.resize(num_states, 0);
    dirty_states.clear();

    if (debug) {
        cout << "Split " << v << " into " << v1 << " and " << v2 << endl;
        cout << "Goal distances: " << goal_distances << endl;
        cout << "Shortest paths: " << shortest_path << endl;
    }

#ifndef NDEBUG
    Transition old_arc = shortest_path[v];
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
    goal_distances[v1] = goal_distances[v2] = goal_distances[v];

    /* Due to the way we select splits, the old shortest path from v1 is
       invalid now, but the path from v2 is still valid. We don't explicitly
       invalidate shortest_path[v1] since v and v1 are the same ID. */
    // TODO: set SP for v1 explicitly? Or at least assert that v==v1?
    set_shortest_path(v2, shortest_path[v]);

    /* Update shortest path transitions to split state. The SPT transition to v1
       will be updated again if v1 is dirty. We therefore prefer reconnecting
       states to v2 instead of v1. */
    // We need to copy the vector since we reuse the index v.
    Children old_children = children[v];
    for (const Transition &old_child : old_children) {
        int u = old_child.target_id;
        int old_cost = convert_to_32_bit_cost(operator_costs[old_child.op_id]);
        int op_id = abstraction.get_operator_between_states(u, v2, old_cost);
        Transition new_parent = (op_id == UNDEFINED)
            ? Transition(old_child.op_id, v1)
            : Transition(op_id, v2);
        set_shortest_path(u, new_parent);
    }

    if (debug) {
        cout << "Goal distances: " << goal_distances << endl;
        cout << "Shortest paths: " << shortest_path << endl;
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
    assert(!count(dirty_candidate.begin(), dirty_candidate.end(), true));

    dirty_candidate.resize(num_states, false);
    dirty_candidate[v1] = true;
    candidate_queue.push(goal_distances[v1], v1);

    while (!candidate_queue.empty()) {
        int state = candidate_queue.pop().second;
        if (debug) {
            cout << "Try to reconnect " << state
                 << " with h=" << goal_distances[state] << endl;
        }
        assert(dirty_candidate[state]);
        assert(goal_distances[state] != INF_COSTS);
        assert(goal_distances[state] != DIRTY);
        bool reconnected = false;
        // Try to reconnect to settled, solvable state.
        abstraction.for_each_outgoing_transition(
            state, [&](const Transition &t) {
                int succ = t.target_id;
                int op_id = t.op_id;
                if (goal_distances[succ] != DIRTY &&
                    add_costs(goal_distances[succ], operator_costs[op_id])
                    == goal_distances[state]) {
                    if (debug) {
                        cout << "Reconnect " << state << " to " << succ << " via " << op_id << endl;
                    }
                    set_shortest_path(state, Transition(op_id, succ));
                    reconnected = true;
                }
                return reconnected;
            });
        if (!reconnected) {
            mark_dirty(state);
            for (const Transition &t : children[state]) {
                int prev = t.target_id;
                assert(shortest_path[prev].target_id == state);
                if (!dirty_candidate[prev] && goal_distances[prev] != DIRTY) {
                    if (debug) {
                        cout << "Add " << prev << " to candidate queue" << endl;
                    }
                    dirty_candidate[prev] = true;
                    candidate_queue.push(goal_distances[prev], prev);
                }
            }
        }
        dirty_candidate[state] = false;

        if (timer.is_expired()) {
            //cout << "Timer expired --> abort incremental search" << endl;
            //return;
        }
    }

    if (debug) {
        cout << "Goal distances: " << goal_distances << endl;
        cout << "Dirty states: " << dirty_states << endl;
    }

#ifndef NDEBUG
    for (int i = 0; i < num_states; ++i) {
        if (goal_distances[i] == DIRTY) {
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
        Cost &dist = goal_distances[state];
        assert(dist == DIRTY);
        Cost min_dist = INF_COSTS;
        for (const Transition &t : abstraction.get_outgoing_transitions(state)) {
            int succ = t.target_id;
            int op_id = t.op_id;
            if (goal_distances[succ] != DIRTY) {
                Cost succ_dist = goal_distances[succ];
                Cost cost = operator_costs[op_id];
                Cost new_dist = add_costs(cost, succ_dist);
                if (new_dist < min_dist) {
                    min_dist = new_dist;
                    set_shortest_path(state, Transition(op_id, succ));
                }
            }
        }
        dist = min_dist;
        if (min_dist != INF_COSTS) {
            open_queue.push(dist, state);
            ++num_orphans;
        }
    }

    if (debug) {
        cout << "dirty: " << dirty_states.size() << endl;
    }
    while (!open_queue.empty()) {
        pair<Cost, int> top_pair = open_queue.pop();
        const Cost g = top_pair.first;
        const int state = top_pair.second;
        assert(count(dirty_states.begin(), dirty_states.end(), state) == 1);
        assert(goal_distances[state] != DIRTY);
        if (g > goal_distances[state])
            continue;
        assert(g == goal_distances[state]);
        assert(g != INF_COSTS);
        if (debug) {
            cout << "  open: " << open_queue.size() << endl;
        }
        for (const Transition &t : abstraction.get_incoming_transitions(state)) {
            int succ = t.target_id;
            int op_id = t.op_id;
            Cost cost = operator_costs[op_id];
            Cost succ_g = add_costs(cost, g);

            if (goal_distances[succ] == DIRTY || succ_g < goal_distances[succ]) {
                assert(count(dirty_states.begin(), dirty_states.end(), succ) == 1);
                goal_distances[succ] = succ_g;
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
    shortest_path.resize(num_states);
    children.resize(num_states);
    goal_distances = vector<Cost>(num_states, INF_COSTS);
    for (int goal : goals) {
        Cost dist = 0;
        goal_distances[goal] = dist;
        set_shortest_path(goal, Transition());
        open_queue.push(dist, goal);
    }
    while (!open_queue.empty()) {
        pair<Cost, int> top_pair = open_queue.pop();
        Cost old_g = top_pair.first;
        int state_id = top_pair.second;

        Cost g = goal_distances[state_id];
        assert(g < INF_COSTS);
        assert(g <= old_g);
        if (g < old_g)
            continue;
        for (const Transition &t : abstraction.get_incoming_transitions(state_id)) {
            int succ_id = t.target_id;
            int op_id = t.op_id;
            Cost op_cost = operator_costs[op_id];
            Cost succ_g = add_costs(g, op_cost);
            if (succ_g < goal_distances[succ_id]) {
                goal_distances[succ_id] = succ_g;
                set_shortest_path(succ_id, Transition(op_id, state_id));
                open_queue.push(succ_g, succ_id);
            }
        }
    }
}

bool ShortestPaths::test_distances(
    const Abstraction &abstraction,
    const unordered_set<int> &goals) {
    assert(none_of(goal_distances.begin(), goal_distances.end(),
                   [](Cost d) {return d == DIRTY;}));
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
        if (goal_distances[i] != INF_COSTS &&
            init_distances[i] != INF &&
            !goals.count(i)) {
            const Transition &t = shortest_path[i];
            if (debug) {
                cout << "SP: " << t << endl;
            }
            assert(t.is_defined());
            Transitions out = abstraction.get_outgoing_transitions(i);
            if (debug) {
                cout << "Outgoing transitions: " << out << endl;
            }
            assert(count(out.begin(), out.end(), t) == 1);
            assert(goal_distances[i] ==
                   add_costs(operator_costs[t.op_id], goal_distances[t.target_id]));
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
            cout << "64-bit distances: " << goal_distances << endl;
            cout << "32-bit rounded:   " << goal_distances_32_bit_rounded_down << endl;
            cout << "32-bit distances: " << goal_distances_32_bit << endl;
            ABORT("Distances are wrong.");
        }
    }

    return true;
}

void ShortestPaths::print_statistics() const {
    if (debug) {
        map<int, int> children_counts;
        for (auto &kids : children) {
            ++children_counts[kids.size()];
        }
        for (auto &pair : children_counts) {
            cout << pair.first << " children: " << pair.second << endl;
        }
    }
    cout << "Goal distances estimated memory usage: "
         << estimate_memory_usage_in_bytes(goal_distances) / 1024 << " KB" << endl;
    cout << "Shortest path tree estimated memory usage: "
         << estimate_memory_usage_in_bytes(shortest_path) / 1024 << " KB" << endl;
    cout << "Shortest path children estimated memory usage: "
         << estimate_vector_of_vector_bytes(children) / 1024 << " KB" << endl;
}
}
