#include "shortest_paths.h"

#include "abstraction.h"
#include "transition_rewirer.h"
#include "utils.h"

#include "../algorithms/priority_queues.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include "../tasks/root_task.h"

#include <cassert>
#include <execution>
#include <map>

using namespace std;

namespace cartesian_abstractions {
const Cost ShortestPaths::INF_COSTS = numeric_limits<Cost>::max();

ShortestPaths::ShortestPaths(
    const TransitionRewirer &rewirer,
    const vector<int> &costs,
    bool store_children,
    bool store_parents,
    const utils::CountdownTimer &timer,
    utils::LogProxy &log)
    : rewirer(rewirer),
      timer(timer),
      log(log),
      use_cache(store_children && store_parents),
      debug(log.is_at_least_debug()),
      task_has_zero_costs(any_of(costs.begin(), costs.end(), [](int c) {return c == 0;})) {
    if (store_parents ^ store_children) {
        cerr << "store_children and store_parents must have the same value." << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    operator_costs.reserve(costs.size());
    for (int cost : costs) {
        operator_costs.push_back(convert_to_64_bit_cost(cost));
    }
    if (log.is_at_least_normal()) {
        log << "Subtask has zero-cost operators: " << boolalpha
            << task_has_zero_costs << endl;
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

void ShortestPaths::resize(int num_states) {
    states.resize(num_states);
    if (use_cache) {
        children.resize(num_states);
        parents.resize(num_states);
    }
}

void ShortestPaths::recompute(
    const Abstraction &abstraction,
    const Goals &goals) {
    open_queue.clear();
    int num_states = abstraction.get_num_states();
    resize(num_states);
    for (StateInfo &state : states) {
        state.goal_distance = INF_COSTS;
    }
    for (int goal : goals) {
        Cost dist = 0;
        states[goal].goal_distance = dist;
        clear_parents(goal);
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
                set_parent(succ_id, Transition(op_id, state_id));
                open_queue.push(succ_g, succ_id);
            } else if (use_cache && succ_g != INF_COSTS && succ_g == states[succ_id].goal_distance) {
                add_parent(succ_id, Transition(op_id, state_id));
            }
        }
    }
}

unique_ptr<Solution> ShortestPaths::extract_solution(
    int init_id, const Goals &goals) {
    // h* = \infty iff goal is unreachable from this state.
    if (states[init_id].goal_distance == INF_COSTS) {
        return nullptr;
    }

    int current_state = init_id;
    unique_ptr<Solution> solution = utils::make_unique_ptr<Solution>();
    assert(!goals.count(current_state));
    while (!goals.count(current_state)) {
        Transition t = states[current_state].parent;
        if (use_cache) {
            assert(!parents[current_state].empty());
            // Pick arbitrary shortest path.
            t = parents[current_state].front();
        }
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

void ShortestPaths::set_parent(int state, const Transition &new_parent) {
    if (debug) {
        log << "Set parent " << new_parent << " for " << state << endl;
    }
    if (use_cache) {
        clear_parents(state);
        add_parent(state, new_parent);
    } else {
        states[state].parent = new_parent;
    }
}

void ShortestPaths::add_parent(int state, const Transition &new_parent) {
    if (debug) {
        log << "Add parent " << new_parent << " for " << state << endl;
    }
    assert(use_cache);
    assert(new_parent.is_defined());
    assert(find(parents[state].begin(), parents[state].end(), new_parent) == parents[state].end());
    parents[state].push_back(new_parent);
    if (use_cache) {
        Transitions &target_children = children[new_parent.target_id];
        assert(find(target_children.begin(), target_children.end(),
                    Transition(new_parent.op_id, state)) == target_children.end());
        target_children.emplace_back(new_parent.op_id, state);
    }
}

void ShortestPaths::remove_child(int state, const Transition &child) {
    if (debug) {
        log << "Remove child " << child << " from " << state << endl;
    }
    assert(use_cache);
    Transitions &state_children = children[state];
    auto it = find(execution::unseq, state_children.begin(), state_children.end(), child);
    assert(it != state_children.end());
    utils::swap_and_pop_from_vector(state_children, it - state_children.begin());
}

void ShortestPaths::remove_parent(int state, const Transition &parent) {
    if (debug) {
        log << "Remove parent " << parent << " from " << state << endl;
    }
    assert(use_cache);
    assert(parent.is_defined());
    auto it = find(execution::unseq, parents[state].begin(), parents[state].end(), parent);
    assert(it != parents[state].end());
    utils::swap_and_pop_from_vector(parents[state], it - parents[state].begin());
}

void ShortestPaths::clear_parents(int state) {
    if (debug) {
        log << "Clear parents for " << state << endl;
    }
    if (use_cache) {
        while (!parents[state].empty()) {
            Transition parent = move(parents[state].back());
            remove_child(parent.target_id, Transition(parent.op_id, state));
            parents[state].pop_back();
        }
    } else {
        set_parent(state, Transition());
    }
}

void ShortestPaths::mark_dirty(int state) {
    if (debug) {
        log << "Mark " << state << " as dirty" << endl;
    }
    assert(!use_cache || parents[state].empty());
    assert(!count(dirty_states.begin(), dirty_states.end(), state));
    states[state].dirty = true;
    dirty_states.push_back(state);
}

void ShortestPaths::update_incrementally(
    const Abstraction &abstraction, int v, int v1, int v2, int var) {
    /*
      Assumption: all h-values correspond to the perfect heuristic for the
      state space before the split.

      orphans holds the newly computed reverse g-values (i.e., h-values) for
      orphaned states, and SETTLED for settled states. A state is orphaned if
      at least one of its possible shortest-path successors is orphaned,
      starting with s_1. We start by assuming g=\infty for all orphaned states.
    */
    int num_states = abstraction.get_num_states();
    resize(num_states);
    dirty_states.clear();

    if (debug) {
        log << "Split " << v << " into " << v1 << " and " << v2 << endl;
    }

    // Copy distance from split state. Distances will be updated if necessary.
    states[v1].goal_distance = states[v2].goal_distance = states[v].goal_distance;

    if (debug) {
        for (size_t state = 0; state < children.size(); ++state) {
            cout << state << " children: " << children[state];
            if (use_cache) {
                cout << endl << state << " parents: " << parents[state] << endl;
            } else {
                cout << ", parent: " << states[state].parent << endl;
            }
        }
        log << "Reconnect children of split node." << endl;
    }

    /* Update shortest path tree (SPT) transitions to v. The SPT transitions
       will be updated again if v1 or v2 are dirty. */
    if (use_cache) {
        rewirer.rewire_transitions(
            children, parents, abstraction.get_states(), v,
            abstraction.get_state(v1), abstraction.get_state(v2), var);
    } else {
        for (int state : {v1, v2}) {
            for (const Transition &incoming : abstraction.get_incoming_transitions(state)) {
                int u = incoming.target_id;
                int op = incoming.op_id;
                const Transition &sp = states[u].parent;
                if (sp.target_id == v &&
                    operator_costs[op] == operator_costs[sp.op_id]) {
                    set_parent(u, Transition(op, state));
                }
            }
        }
    }

    /*
      If we split a state that's an ancestor of the initial state in the SPT,
      we know that exactly one of v1 or v2 is still settled. This allows us to
      push only one of them into the candidate queue. With splits that don't
      consider the SPT, we cannot make this optimization anymore and need to
      add both states to the candidate queue.
    */
    assert(candidate_queue.empty());
    assert(all_of(states.begin(), states.end(), [](const StateInfo &s) {
                      return !s.dirty || s.goal_distance == INF_COSTS;
                  }));

    states[v1].dirty_candidate = true;
    states[v2].dirty_candidate = true;
    candidate_queue.push(states[v1].goal_distance, v1);
    candidate_queue.push(states[v2].goal_distance, v2);

    while (!candidate_queue.empty()) {
        int state = candidate_queue.pop().second;
        if (debug) {
            log << "Try to reconnect " << state
                << " with h=" << states[state].goal_distance << endl;
        }
        assert(states[state].dirty_candidate);
        assert(states[state].goal_distance != INF_COSTS);
        assert(!states[state].dirty);
        bool reconnected = false;
        // Try to reconnect to settled, solvable state.
        if (use_cache) {
            // Remove invalid transitions from children and parents vectors.
            parents[state].erase(
                remove_if(
                    parents[state].begin(), parents[state].end(),
                    [&](const Transition &parent) {
                        assert(abstraction.has_transition(state, parent.op_id, parent.target_id));
                        bool valid_parent = !states[parent.target_id].dirty;
                        if (!valid_parent) {
                            remove_child(parent.target_id, Transition(parent.op_id, state));
                        }
                        return !valid_parent;
                    }), parents[state].end());
            reconnected = !parents[state].empty();
        } else {
            for (const Transition &t : abstraction.get_outgoing_transitions(state)) {
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
                    set_parent(state, Transition(op_id, succ));
                    reconnected = true;
                    break;
                }
            }
        }
        if (debug) {
            log << "Reconnected: " << boolalpha << reconnected << endl;
        }
        if (!reconnected) {
            mark_dirty(state);

            if (use_cache) {
                if (g_hacked_sort_transitions) {
                    sort(execution::unseq, children[state].begin(), children[state].end());
                }
                for (const Transition &t : children[state]) {
                    int prev = t.target_id;
                    if (!states[prev].dirty_candidate && !states[prev].dirty) {
                        if (debug) {
                            log << "Add " << prev << " to candidate queue" << endl;
                        }
                        states[prev].dirty_candidate = true;
                        candidate_queue.push(states[prev].goal_distance, prev);
                    }
                }
            } else {
                for (const Transition &t : abstraction.get_incoming_transitions(state)) {
                    int prev = t.target_id;
                    if (!states[prev].dirty_candidate &&
                        !states[prev].dirty &&
                        states[prev].parent.target_id == state) {
                        if (debug) {
                            log << "Add " << prev << " to candidate queue" << endl;
                        }
                        states[prev].dirty_candidate = true;
                        candidate_queue.push(states[prev].goal_distance, prev);
                    }
                }
            }
        }
        states[state].dirty_candidate = false;

        if (timer.is_expired()) {
            // Up to here all goal distances are always lower bounds, so we can abort at any time.
            cout << "Timer expired --> abort incremental search" << endl;
            return;
        }
    }

#ifndef NDEBUG
    /*
      We use dirty_states to efficiently loop over dirty states. Check that all
      solvable states marked as dirty are part of the vector. Since we don't
      explicitly reset dirty states, the check doesn't hold in the other
      direction.
    */
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
                    set_parent(state, Transition(op_id, succ));
                } else if (use_cache && new_dist != INF_COSTS && new_dist == min_dist) {
                    add_parent(state, Transition(op_id, succ));
                }
            }
        }
        states[state].goal_distance = min_dist;
        if (min_dist != INF_COSTS) {
            open_queue.push(min_dist, state);
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
                set_parent(succ, Transition(op_id, state));
                open_queue.push(succ_g, succ);
            } else if (use_cache && states[succ].dirty &&
                       succ_g == states[succ].goal_distance && succ_g != INF_COSTS) {
                add_parent(succ, Transition(op_id, state));
            }
        }
    }
}

Cost ShortestPaths::get_64bit_goal_distance(int abstract_state_id) const {
    return states[abstract_state_id].goal_distance;
}

int ShortestPaths::get_32bit_goal_distance(int abstract_state_id) const {
    return convert_to_32_bit_cost(get_64bit_goal_distance(abstract_state_id));
}

bool ShortestPaths::is_optimal_transition(int start_id, int op_id, int target_id) const {
    return states[start_id].goal_distance - operator_costs[op_id] == states[target_id].goal_distance;
}

OptimalTransitions ShortestPaths::get_optimal_transitions(
    const Abstraction &abstraction, int state) const {
    OptimalTransitions transitions;
    if (use_cache) {
        for (const Transition &t : parents[state]) {
            transitions[t.op_id].push_back(t.target_id);
        }
        if (g_hacked_sort_transitions) {
            for (auto &[op_id, transitions_for_op]: transitions) {
                sort(execution::unseq, transitions_for_op.begin(), transitions_for_op.end());
            }
        }
    } else {
        for (const Transition &t : abstraction.get_outgoing_transitions(state)) {
            if (is_optimal_transition(state, t.op_id, t.target_id)) {
                transitions[t.op_id].push_back(t.target_id);
            }
        }
    }
    return transitions;
}

#ifndef NDEBUG
bool ShortestPaths::test_distances(
    const Abstraction &abstraction,
    const Goals &goals) {
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

    for (int v = 0; v < num_states; ++v) {
        if (debug) {
            log << "Test state " << v << endl;
        }
        if (debug && use_cache) {
            cout << "children: " << children[v] << endl;
        }
        if (use_cache) {
            if (debug) {
                cout << "parents: " << parents[v] << endl;
            }
            for (const Transition &parent : parents[v]) {
                int w = parent.target_id;
                int op_id = parent.op_id;
                assert(count(children[w].begin(), children[w].end(), Transition(op_id, v)) == 1);
                assert(abstraction.has_transition(v, op_id, w));
            }
            for (const Transition &child : children[v]) {
                int u = child.target_id;
                int op_id = child.op_id;
                assert(count(parents[u].begin(), parents[u].end(), Transition(op_id, v)) == 1);
                assert(abstraction.has_transition(u, op_id, v));
            }
        } else {
            if (states[v].goal_distance == INF_COSTS ||
                init_distances[v] == INF ||
                goals.count(v)) {
                continue;
            }
            const Transition &t = states[v].parent;
            if (debug) {
                log << "Parent: " << t << endl;
            }
            assert(t.is_defined());
            Transitions out = abstraction.get_outgoing_transitions(v);
            if (debug) {
                log << "Outgoing transitions: " << out << endl;
            }
            assert(count(out.begin(), out.end(), t) == 1);
            assert(states[v].goal_distance ==
                   add_costs(operator_costs[t.op_id], states[t.target_id].goal_distance));
        }
    }

    vector<int> goal_distances_32_bit = compute_goal_distances(abstraction, costs, goals);
    vector<int> goal_distances_32_bit_rounded_down = get_goal_distances();

    for (int i = 0; i < num_states; ++i) {
        if (goal_distances_32_bit_rounded_down[i] != goal_distances_32_bit[i] &&
            init_distances[i] != INF) {
            log << "32-bit INF: " << INF << endl;
            log << "64-bit 0: " << convert_to_64_bit_cost(0) << endl;
            log << "64-bit 1: " << convert_to_64_bit_cost(1) << endl;
            log << "64-bit INF: " << INF_COSTS << endl;
            log << "32-bit rounded:   " << goal_distances_32_bit_rounded_down << endl;
            log << "32-bit distances: " << goal_distances_32_bit << endl;
            ABORT("Distances are wrong.");
        }
    }
    return true;
}
#endif

void ShortestPaths::print_statistics() const {
    if (log.is_at_least_verbose()) {
        map<int, int> children_counts;
        for (const auto &c : children) {
            children_counts[c.size()] += 1;
        }
        log << "SPT children: " << children_counts << endl;
        map<int, int> parents_counts;
        for (const auto &p : parents) {
            parents_counts[p.size()] += 1;
        }
        log << "SPT parents: " << parents_counts << endl;
    }
}

vector<int> compute_goal_distances(
    const Abstraction &abstraction,
    const vector<int> &costs,
    const unordered_set<int> &start_ids) {
    vector<int> distances(abstraction.get_num_states(), INF);
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
        for (const Transition &transition : abstraction.get_incoming_transitions(state_id)) {
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
