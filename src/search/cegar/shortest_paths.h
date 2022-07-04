#ifndef CEGAR_SHORTEST_PATHS_H
#define CEGAR_SHORTEST_PATHS_H

#include "transition.h"
#include "types.h"

#include <cassert>
#include <memory>
#include <queue>
#include <vector>

namespace utils {
class LogProxy;
}

namespace cegar {
/*
  The code below requires that all operators have positive cost. Negative
  operators are of course tricky, but 0-cost operators are somewhat tricky,
  too. In particular, given perfect g and h values, we want to know which
  operators make progress towards the goal, and this is easy to do if all
  operator costs are positive (then *all* operators that lead to a state with
  the same f value as the current one make progress towards the goal, in the
  sense that following those operators will necessarily take us to the goal on
  a path with strictly decreasing h values), but not if they may be 0 (consider
  the case where all operators cost 0: then the f* values of all alive states
  are 0, so they give us no guidance towards the goal).

  If the assumption of no 0-cost operators is violated, the easiest way to
  address this is to replace all 0-cost operators with operators of cost
  epsilon, where epsilon > 0 is small enough that "rounding down" epsilons
  along a shortest path always results in the correct original cost. With
  original integer costs, picking epsilon <= 1/N for a state space with N
  states is sufficient for this. In our actual implementation, we do not want
  to use floating-point numbers, and if we stick with 32-bit integers for path
  costs, we could run into range issues. Therefore, we use 64-bit integers,
  scale all original operator costs by 2^32 and use epsilon = 1.
*/

class HeapQueue {
    using Entry = std::pair<Cost, int>;

    struct compare_func {
        bool operator()(const Entry &lhs, const Entry &rhs) const {
            return lhs.first > rhs.first;
        }
    };

    // We inherit to gain access to the protected underlying container c.
    class Heap
        : public std::priority_queue<Entry, std::vector<Entry>, compare_func> {
        friend class HeapQueue;
    };

    Heap heap;
public:
    void push(Cost key, int value) {
        heap.push(std::make_pair(key, value));
    }

    Entry pop() {
        assert(!heap.empty());
        Entry result = heap.top();
        heap.pop();
        return result;
    }

    bool empty() const {
        return heap.empty();
    }

    int size() const {
        return heap.size();
    }

    void clear() {
        heap.c.clear();
    }
};


class ShortestPaths {
    static const Cost DIRTY;

    utils::LogProxy &log;
    const bool debug;
    const bool task_has_zero_costs;
    std::vector<Cost> operator_costs;

    // Keep data structures around to avoid reallocating them.
    HeapQueue candidate_queue;
    HeapQueue open_queue;
    std::vector<Cost> goal_distances;
    std::vector<bool> dirty_candidate;
    std::vector<int> dirty_states;
    Transitions shortest_path;

    static Cost add_costs(Cost a, Cost b);
    int convert_to_32_bit_cost(Cost cost) const;
    Cost convert_to_64_bit_cost(int cost) const;

    void mark_dirty(int state);
public:
    ShortestPaths(const std::vector<int> &costs, utils::LogProxy &log);

    // Use Dijkstra's algorithm to compute the shortest path tree from scratch.
    void recompute(
        const std::vector<Transitions> &transitions,
        const std::unordered_set<int> &goals);
    // Reflect the split of v into v1 and v2.
    void update_incrementally(
        const std::vector<Transitions> &in,
        const std::vector<Transitions> &out,
        int v, int v1, int v2);
    // Extract solution from shortest path tree.
    std::unique_ptr<Solution> extract_solution(
        int init_id,
        const Goals &goals);

    Cost get_64bit_goal_distance(int abstract_state_id) const;
    int get_32bit_goal_distance(int abstract_state_id) const;
    bool is_optimal_transition(int start_id, int op_id, int target_id) const;

    // For debugging.
    bool test_distances(
        const std::vector<Transitions> &in,
        const std::vector<Transitions> &out,
        const std::unordered_set<int> &goals);
};
}

#endif
