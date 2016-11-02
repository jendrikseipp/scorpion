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

std::ostream &operator<<(std::ostream &os, const Transition &transition);

class Abstraction {
    // State-changing transitions.
    const std::vector<std::vector<Transition>> backward_graph;

    // Operators inducing state-changing transitions.
    const std::vector<int> active_operators;

    // Operators inducing self-loops. May overlap with active operators.
    const std::vector<int> looping_operators;

    const std::vector<int> goal_states;
    const int num_operators;
    const bool use_general_costs;

    mutable AdaptiveQueue<int> queue;

    std::vector<int> compute_h_values(const std::vector<int> &costs) const;
    std::vector<int> compute_saturated_costs(const std::vector<int> &h_values) const;

public:
    Abstraction(
        std::vector<std::vector<Transition>> &&backward_graph,
        std::vector<int> &&looping_operators,
        std::vector<int> &&goal_states,
        int num_operators);

    Abstraction(const Abstraction &) = delete;

    std::pair<std::vector<int>, std::vector<int>>
        compute_goal_distances_and_saturated_costs(
            const std::vector<int> &costs) const;

    const std::vector<int> &get_active_operators() const;

    int get_num_states() const;

    void dump() const;
};
}

#endif
