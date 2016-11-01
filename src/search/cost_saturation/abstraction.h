#ifndef COST_SATURATION_ABSTRACTION_H
#define COST_SATURATION_ABSTRACTION_H

#include "../priority_queue.h"

#include <limits>
#include <vector>

namespace cost_saturation {
struct Transition {
    int op;
    int state;

    Transition(int op, int state)
        : op(op),
          state(state) {
    }
};

class Abstraction {
    const std::vector<std::vector<Transition>> backward_graph;
    const std::vector<int> looping_operators;
    const std::vector<int> goal_states;
    const int num_operators;
    const bool use_general_costs;

    mutable AdaptiveQueue<int> queue;

    std::vector<int> compute_saturated_costs(const std::vector<int> &h_values) const;

public:
    Abstraction(
        std::vector<std::vector<Transition>> &&backward_graph,
        std::vector<int> &&looping_operators,
        std::vector<int> &&goal_states,
        int num_operators);

    Abstraction(const Abstraction &) = delete;

    std::vector<int> compute_h_values(const std::vector<int> &costs) const;

    std::pair<std::vector<int>, std::vector<int>>
        compute_goal_distances_and_saturated_costs(
            const std::vector<int> &costs) const;

    std::vector<bool> compute_active_operators();

    int get_num_states() const;

    void dump() const;
};
}

#endif
