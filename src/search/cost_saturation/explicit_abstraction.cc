#include "utils.h"
#include "explicit_abstraction.h"

#include "abstraction.h"
#include "types.h"

#include "../utils/collections.h"
#include "../utils/strings.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <ostream>
#include <unordered_set>

#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
// Label maps
phmap::flat_hash_map<std::vector<int>, int, VectorHash> ops_to_label_id;
phmap::flat_hash_map<int, Label> label_id_to_label;
int next_label_id = -1;

// Tracking of some numbers
int num_transitions_sub = 0;
int num_single_transitions = 0;
unordered_set<int> op_set;
unordered_set<int> op_set_single;
int num_label = 0;
int num_new_label = 0;

static void dijkstra_search(
    const vector<vector<Successor>> &graph,
    const vector<int> &costs,
    priority_queues::AdaptiveQueue<int> &queue,
    vector<int> &distances) {
    assert(all_of(costs.begin(), costs.end(), [](int c) {return c >= 0;}));
    while (!queue.empty()) {
        pair<int, int> top_pair = queue.pop();
        int distance = top_pair.first;
        int state = top_pair.second;
        int state_distance = distances[state];
        assert(state_distance <= distance);
        if (state_distance < distance) {
            continue;
        }
        for (const Successor &transition : graph[state]) {
            int successor = transition.state;
            int op = transition.op;
            int op_cost;
            if (op >= 0) {
                assert(utils::in_bounds(op, costs));
                op_cost = costs[op];
            } else {
                auto it = label_id_to_label.find(op);
                assert(it != label_id_to_label.end());
                op_cost = it->second.cost;
            }
            assert(op_cost >= 0);
            int successor_distance = (op_cost == INF) ? INF : state_distance + op_cost;
            assert(successor_distance >= 0);
            if (distances[successor] > successor_distance) {
                distances[successor] = successor_distance;
                queue.push(successor_distance, successor);
            }
        }
    }
}

ostream &operator<<(ostream &os, const Successor &successor) {
    os << "(" << successor.op << ", " << successor.state << ")";
    return os;
}

static vector<bool> get_active_operators_from_graph(
    const vector<vector<Successor>> &backward_graph, int num_ops) {
    vector<bool> active_operators(num_ops, false);
    int num_states = backward_graph.size();
    for (int target = 0; target < num_states; ++target) {
        for (const Successor &transition : backward_graph[target]) {
            int op_id = transition.op;
            if (op_id >= 0) {
                assert(utils::in_bounds(op_id,active_operators));
                active_operators[op_id] = true;
            } else {
                auto it = label_id_to_label.find(op_id);
                assert(it != label_id_to_label.end());
                for (int actual_op : it->second.operators) {
                    assert(utils::in_bounds(actual_op,active_operators));
                    active_operators[actual_op] = true;
                }
            }
        }
    }
    return active_operators;
}

static std::vector<std::vector<Successor>> label_reduction(
    std::vector<std::vector<Successor>> &graph, const std::vector<int> &costs) {
    num_transitions_sub = 0;
    num_single_transitions = 0;
    op_set;
    op_set_single;
    num_label = 0;
    num_new_label = 0;
    // Retrieve non-looping transitions.
    vector<vector<Successor>> new_graph(graph.size());
    
    // Map from (src, target) to list of operators
    phmap::flat_hash_map<pair<int, int>, vector<int>> transition_groups;
    for (std::size_t target = 0; target < graph.size(); ++target) {
        for (const Successor &succ : graph[target]) {
            num_transitions_sub++;
            transition_groups[{succ.state, target}].push_back(succ.op);
        }
    }
    for (const auto &[src_target, op_list] : transition_groups) {
        const auto &[src, target] = src_target;
        
        vector<int> ops = op_list;
        sort(ops.begin(), ops.end());
        
        int label_id;
        if (ops.size() == 1) { // Param for min_label_size
            label_id = ops[0];
            num_single_transitions++;
            op_set_single.insert(label_id);
            op_set.insert(label_id);
        } else {
            num_label++;
            if (ops_to_label_id.count(ops)) {
                label_id = ops_to_label_id[ops];
            } else {
                label_id = next_label_id--;
                int label_cost = INF;
                for (int op_id : ops) {
                    label_cost = min(label_cost, costs[op_id]); 
                    op_set.insert(op_id);
                }
                assert(label_cost >= 0);
                ops_to_label_id[ops] = label_id;
                label_id_to_label[label_id] = Label(std::move(ops), label_cost);
                num_new_label++;
            }
        }
        new_graph[target].emplace_back(label_id, src);
    }    
    for (size_t target = 0; target < graph.size(); ++target) {
        new_graph[target].shrink_to_fit();
		// cout << "Graph: " << target << new_graph[target] << endl;
    }
    return new_graph;
}

ExplicitAbstraction::ExplicitAbstraction(
    unique_ptr<AbstractionFunction> abstraction_function,
    vector<vector<Successor>> &&backward_graph_,
    vector<bool> &&looping_operators,
    vector<int> &&goal_states)
    : Abstraction(move(abstraction_function)),
      backward_graph(move(backward_graph_)),
      active_operators(get_active_operators_from_graph(
                           backward_graph, looping_operators.size())),
      looping_operators(move(looping_operators)),
      goal_states(move(goal_states)) {
#ifndef NDEBUG
    for (int target = 0; target < get_num_states(); ++target) {
        // Check that no transition is stored multiple times.
        vector<Successor> copied_transitions = this->backward_graph[target];
        sort(copied_transitions.begin(), copied_transitions.end());
        assert(utils::is_sorted_unique(copied_transitions));
        // Check that we don't store self-loops.
        assert(all_of(copied_transitions.begin(), copied_transitions.end(),
                      [target](const Successor &succ) {return succ.state != target;}));
    }
#endif
}

ExplicitAbstraction::ExplicitAbstraction(
    unique_ptr<AbstractionFunction> abstraction_function,
    vector<vector<Successor>> &&backward_graph_,
    vector<bool> &&looping_operators,
    vector<int> &&goal_states,
    const vector<int> &costs)
    : Abstraction(move(abstraction_function)),
      backward_graph(move(label_reduction(backward_graph_, costs))),
      active_operators(get_active_operators_from_graph(
                           backward_graph, looping_operators.size())),
      looping_operators(move(looping_operators)),
      goal_states(move(goal_states)) {
#ifndef NDEBUG
    for (int target = 0; target < get_num_states(); ++target) {
        // Check that no transition is stored multiple times.
        vector<Successor> copied_transitions = this->backward_graph[target];
        sort(copied_transitions.begin(), copied_transitions.end());
        assert(utils::is_sorted_unique(copied_transitions));
        // Check that we don't store self-loops.
        assert(all_of(copied_transitions.begin(), copied_transitions.end(),
                      [target](const Successor &succ) { return succ.state != target; }));
    }
#endif
}

vector<int> ExplicitAbstraction::compute_goal_distances(const vector<int> &costs) const {
    vector<int> goal_distances(get_num_states(), INF);
    queue.clear();
    for (int goal_state : goal_states) {
        goal_distances[goal_state] = 0;
        queue.push(0, goal_state);
    }
    dijkstra_search(backward_graph, costs, queue, goal_distances);
    return goal_distances;
}

vector<int> ExplicitAbstraction::compute_saturated_costs(
    const vector<int> &h_values) const {
    int num_operators = get_num_operators();
    vector<int> saturated_costs(num_operators, -INF);
    vector<int> saturated_label_costs(label_id_to_label.size() + 1, -INF);
    unordered_set<int> updated_label_indices;

    /* To prevent negative cost cycles we ensure that all operators
       inducing self-loops have non-negative costs. */
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        if (looping_operators[op_id]) {
            saturated_costs[op_id] = 0;
        }
    }
    // prevent negative cost cycles for labels ...
    // for (auto &[label_id, label] : label_id_to_label) {
    //     if (!label.operators.empty()) {
    //         int op_id = label.operators.front();
    //         if (looping_operators[op_id]) {
    //             saturated_label_costs[-label_id] = 0;
    //         }
    //     }
    // }

    int num_states = backward_graph.size();
    for (int target = 0; target < num_states; ++target) {
        assert(utils::in_bounds(target, h_values));
        int target_h = h_values[target];
        if (target_h == INF) {
            continue;
        }

        for (const Successor &transition : backward_graph[target]) {
            int op_id = transition.op;
            int src = transition.state;
            assert(utils::in_bounds(src, h_values));
            int src_h = h_values[src];
            if (src_h == INF) {
                continue;
            }

            const int needed = src_h - target_h;
            if (op_id >= 0) {
                saturated_costs[op_id] = max(saturated_costs[op_id], needed);
            } else {
                // saturated_label_costs[-op_id] = max(saturated_label_costs[-op_id], needed);
                int label_idx = -op_id;
                if (saturated_label_costs[label_idx] < needed) {
                    saturated_label_costs[label_idx] = needed;
                    updated_label_indices.insert(label_idx);
                }
                // auto it = label_id_to_label.find(op_id);
                // assert(it != label_id_to_label.end());
                // for (int actual_op : it->second.operators) {
                //     saturated_costs[actual_op] = max(saturated_costs[actual_op], needed);
                // }
            }
        }
    }
    // unpack saturated_label_costs
    for (int idx : updated_label_indices) {
        assert(utils::in_bounds(idx, saturated_label_costs));
        int label_cost = saturated_label_costs[idx];

        auto it = label_id_to_label.find(-idx);
        assert(it != label_id_to_label.end());
        for (int op_id : it->second.operators) {
            assert(utils::in_bounds(op_id, saturated_costs));
            saturated_costs[op_id] = max(saturated_costs[op_id], label_cost);
        }
    }
    return saturated_costs;
}

int ExplicitAbstraction::get_num_operators() const {
    return looping_operators.size();
}

int ExplicitAbstraction::get_num_states() const {
    return backward_graph.size();
}

bool ExplicitAbstraction::operator_is_active(int op_id) const {
    return active_operators[op_id];
}

bool ExplicitAbstraction::operator_induces_self_loop(int op_id) const {
    return looping_operators[op_id];
}

void ExplicitAbstraction::for_each_transition(const TransitionCallback &callback) const {
    int num_states = get_num_states();
    for (int target = 0; target < num_states; ++target) {
        for (const Successor &transition : backward_graph[target]) {
            int op_id = transition.op;
            int src = transition.state;
            if (op_id >= 0) {
                callback(Transition(src, op_id, target));
            } else {
                auto it = label_id_to_label.find(op_id);
                assert(it != label_id_to_label.end());
                for (int actual_op : it->second.operators) {
                    callback(Transition(src, actual_op, target));
                }
            }
        }
    }
}

const vector<int> &ExplicitAbstraction::get_goal_states() const {
    return goal_states;
}

void ExplicitAbstraction::dump() const {
    int num_states = get_num_states();

    cout << "States: " << num_states << endl;
    cout << "Goal states: " << goal_states.size() << endl;
    cout << "Operators inducing state-changing transitions: "
         << count(active_operators.begin(), active_operators.end(), true) << endl;
    cout << "Operators inducing self-loops: "
         << count(looping_operators.begin(), looping_operators.end(), true) << endl;

    vector<bool> is_goal(num_states, false);
    for (int goal : goal_states) {
        is_goal[goal] = true;
    }

    cout << "digraph transition_system";
    cout << " {" << endl;
    for (int i = 0; i < num_states; ++i) {
        cout << "    node [shape = " << (is_goal[i] ? "doublecircle" : "circle")
             << "] " << i << ";" << endl;
    }
    for (int target = 0; target < num_states; ++target) {
        unordered_map<int, vector<int>> parallel_transitions;
        for (const Successor &succ : backward_graph[target]) {
            int src = succ.state;
            parallel_transitions[src].push_back(succ.op);
        }
        for (const auto &pair : parallel_transitions) {
            int src = pair.first;
            const vector<int> &operators = pair.second;
            cout << "    " << src << " -> " << target
                 << " [label = \"" << utils::join(operators, "_") << "\"];" << endl;
        }
    }
    cout << "}" << endl;
}
}
