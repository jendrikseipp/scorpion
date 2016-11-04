#ifndef COST_SATURATION_EXPLICIT_ABSTRACTION_H
#define COST_SATURATION_EXPLICIT_ABSTRACTION_H

#include "abstraction.h"

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

class ExplicitAbstraction : public Abstraction {
    // State-changing transitions.
    const std::vector<std::vector<Transition>> backward_graph;

    // Operators inducing state-changing transitions.
    const std::vector<int> active_operators;

    // Operators inducing self-loops. May overlap with active operators.
    const std::vector<int> looping_operators;

    const std::vector<int> goal_states;
    const int num_operators;

    mutable AdaptiveQueue<int> queue;

protected:
    virtual std::vector<int> compute_saturated_costs(
        const std::vector<int> &h_values) const override;

public:
    ExplicitAbstraction(
        std::vector<std::vector<Transition>> &&backward_graph,
        std::vector<int> &&looping_operators,
        std::vector<int> &&goal_states,
        int num_operators);

    virtual std::vector<int> compute_h_values(
        const std::vector<int> &costs) const override;

    virtual const std::vector<int> &get_active_operators() const override;

    virtual int get_num_states() const override;

    virtual void dump() const override;
};
}

#endif
