#ifndef COST_SATURATION_ABSTRACTION_H
#define COST_SATURATION_ABSTRACTION_H

#include "../priority_queue.h"

#include <limits>
#include <vector>

namespace cost_saturation {
// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();

struct Transition {
    int op;
    int target;

    Transition(int op, int target)
        : op(op),
          target(target) {
    }

    bool operator==(const Transition &other) {
        return op == other.op && target == other.target;
    }
};

class Abstraction {
    const std::vector<std::vector<Transition>> backward_graph;
    const std::vector<int> looping_operators;
    const std::vector<int> goal_states;
    const int num_operators;
    const bool use_general_costs;

    mutable AdaptiveQueue<int> queue;

    std::vector<int> compute_h_values(const std::vector<int> &costs) const;
    std::vector<int> compute_saturated_costs(const std::vector<int> &h_values) const;

public:
    Abstraction(int num_operators);

    Abstraction(const Abstraction &) = delete;

    std::pair<std::vector<int>, std::vector<int>> compute_h_values_and_saturated_costs(
        const std::vector<int> &costs);
};
}

#endif
