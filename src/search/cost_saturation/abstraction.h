#ifndef COST_SATURATION_ABSTRACTION_H
#define COST_SATURATION_ABSTRACTION_H

#include <vector>

class State;

namespace cost_saturation {
struct ExplicitTransition {
    int src;
    int op;
    int target;

    ExplicitTransition(int src, int op, int target)
        : src(src),
          op(op),
          target(target) {
    }
};


class Abstraction {
protected:
    std::vector<int> goal_states;

    // Operators inducing state-changing transitions.
    std::vector<int> active_operators;

    // Operators inducing self-loops. May overlap with active operators.
    std::vector<int> looping_operators;

    const int num_operators;

    const bool use_general_costs;

    virtual std::vector<int> compute_saturated_costs(
        const std::vector<int> &h_values) const = 0;

public:
    explicit Abstraction(int num_operators);
    virtual ~Abstraction();

    Abstraction(const Abstraction &) = delete;

    virtual int get_abstract_state_id(const State &concrete_state) const = 0;

    virtual std::vector<int> compute_h_values(
        const std::vector<int> &costs) const = 0;

    std::pair<std::vector<int>, std::vector<int>>
    compute_goal_distances_and_saturated_costs(
        const std::vector<int> &costs) const;

    const std::vector<int> &get_active_operators() const {
        return active_operators;
    }

    const std::vector<int> &get_looping_operators() const {
        return looping_operators;
    }

    virtual std::vector<ExplicitTransition> get_transitions() const = 0;

    virtual int get_num_states() const = 0;

    const std::vector<int> &get_goal_states() const;

    virtual void release_transition_system_memory();

    virtual void dump() const = 0;
};
}

#endif
