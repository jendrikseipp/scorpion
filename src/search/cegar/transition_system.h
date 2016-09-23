#ifndef CEGAR_TRANSITION_SYSTEM_H
#define CEGAR_TRANSITION_SYSTEM_H

#include <memory>
#include <vector>

class AbstractTask;
class State;

namespace cegar {
class Abstraction;
class RefinementHierarchy;

struct ExplicitTransition {
    int start;
    int op;
    int end;

    ExplicitTransition(int start, int op, int end)
      : start(start), op(op), end(end) {
    }
};

class TransitionSystem {
    int num_states;
    const std::shared_ptr<RefinementHierarchy> refinement_hierarchy;
    std::vector<int> h_values;
    std::vector<bool> operator_induces_self_loop;
    std::vector<ExplicitTransition> transitions;
    std::vector<int> goal_indices;

public:
    explicit TransitionSystem(Abstraction &&abstraction);
    TransitionSystem(const TransitionSystem &other) = delete;

    int get_num_abstract_states() const;
    int get_abstract_state_index(const State &concrete_state) const;
    bool is_dead_end(const State &concrete_state) const;
    bool induces_self_loop(int op_id) const;
    const std::vector<int> &get_goal_indices() const;
    const std::vector<ExplicitTransition> &get_transitions() const;

    void release_memory();
};
}

#endif
