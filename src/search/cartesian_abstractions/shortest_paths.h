#ifndef CARTESIAN_ABSTRACTIONS_SHORTEST_PATHS_H
#define CARTESIAN_ABSTRACTIONS_SHORTEST_PATHS_H

#include "transition.h"
#include "types.h"

#include <cassert>
#include <memory>
#include <queue>
#include <vector>

namespace utils {
class CountdownTimer;
class LogProxy;
}

namespace cartesian_abstractions {
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

class Abstraction;
class TransitionRewirer;

using Cost = uint64_t;

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


struct StateInfo {
    Cost goal_distance;
    bool dirty_candidate;
    bool dirty;
    Transition parent;

    StateInfo()
        : goal_distance(0),
          dirty_candidate(false),
          dirty(false) {
    }
};
static_assert(
    sizeof(StateInfo) == sizeof(Cost) + sizeof(void *) + sizeof(Transition),
    "StateInfo has unexpected size");


class ShortestPaths {
    static const Cost INF_COSTS;

    const utils::CountdownTimer &timer;
    utils::LogProxy &log;
    const bool store_children;
    const bool store_parents;
    const bool debug;
    const bool task_has_zero_costs;
    std::vector<Cost> operator_costs;

    // Keep data structures around to avoid reallocating them.
    HeapQueue candidate_queue;
    HeapQueue open_queue;
    std::vector<int> dirty_states;

    std::deque<StateInfo> states;
    std::deque<Transitions> children;
    std::deque<Transitions> parents;
    std::unique_ptr<TransitionRewirer> rewirer;

    static Cost add_costs(Cost a, Cost b);
    int convert_to_32_bit_cost(Cost cost) const;
    Cost convert_to_64_bit_cost(int cost) const;

    void resize(int num_states);
    void set_parent(int state, const Transition &new_parent);
    void add_parent(int state, const Transition &new_parent);
    void remove_parent(int state, const Transition &parent);
    void clear_parents(int state);
    void remove_child(int state, const Transition &child);
    void mark_dirty(int state);
    void mark_orphaned_predecessors(const Abstraction &abstraction, int state);
    bool is_optimal_transition(int start_id, int op_id, int target_id) const;

public:
    ShortestPaths(
        const std::vector<int> &costs,
        bool store_children,
        bool store_parents,
        const utils::CountdownTimer &timer,
        utils::LogProxy &log);

    // Use Dijkstra's algorithm to compute the shortest path tree from scratch.
    void recompute(
        const Abstraction &abstraction,
        const Goals &goals);
    // Reflect the split of v into v1 and v2.
    void update_incrementally(
        const Abstraction &abstraction, int v, int v1, int v2, int var);
    // Extract solution from shortest path tree.
    std::unique_ptr<Solution> extract_solution(
        int init_id,
        const Goals &goals);

    std::vector<int> get_goal_distances() const;

    Cost get_64bit_goal_distance(int abstract_state_id) const;
    int get_32bit_goal_distance(int abstract_state_id) const;
    OptimalTransitions get_optimal_transitions(
        const Abstraction &abstraction, int state) const;

#ifndef NDEBUG
    bool test_distances(
        const Abstraction &abstraction,
        const Goals &goals);
#endif

    void print_statistics() const;
};

extern std::vector<int> compute_goal_distances(
    const Abstraction &abstraction,
    const std::vector<int> &costs,
    const std::unordered_set<int> &start_ids);
}

#endif
