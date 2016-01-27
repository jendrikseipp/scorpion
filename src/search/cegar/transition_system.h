#ifndef CEGAR_TRANSITION_SYSTEM_H
#define CEGAR_TRANSITION_SYSTEM_H

#include <vector>

class OperatorProxy;
class State;

namespace cegar {
class Abstraction;

struct Transition {
    int start;
    int op;
    int end;

    Transition(int start, int op, int end)
      : start(start), op(op), end(end) {
    }
    ~Transition() = default;
};

class TransitionSystem {

public:
    explicit TransitionSystem(const Abstraction &abstraction);
    ~TransitionSystem() = default;

    int get_num_abstract_states() const;

    int get_abstract_state_index(const State &concrete_state) const;

    bool is_dead_end(const State &concrete_state) const;

    bool induces_self_loop(const OperatorProxy &op) const;

    const std::vector<int> &get_goal_indices() const;

    const std::vector<Transition> &get_transitions() const;
};
}

#endif
