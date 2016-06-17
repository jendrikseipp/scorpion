#ifndef CEGAR_TRANSITION_SYSTEM_H
#define CEGAR_TRANSITION_SYSTEM_H

#include "refinement_hierarchy.h"

#include "../task_proxy.h"

#include "../utils/collections.h"

#include <limits>
#include <unordered_map>
#include <vector>

class AbstractTask;
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
    std::shared_ptr<AbstractTask> task;
    TaskProxy task_proxy;
    int num_states;
    const RefinementHierarchy refinement_hierarchy;
    std::unordered_map<Node *, int> node_to_state_id;
    std::vector<bool> operator_induces_self_loop;
    std::vector<Transition> transitions;
    std::vector<int> goal_indices;

public:
    TransitionSystem(
        const std::shared_ptr<AbstractTask> &task, Abstraction &&abstraction);
    ~TransitionSystem() = default;

    int get_num_abstract_states() const {
        return num_states;
    }

    int get_abstract_state_index(const State &concrete_state) const;

    bool is_dead_end(const State &concrete_state) const;

    bool induces_self_loop(int op_id) const;

    const std::vector<int> &get_goal_indices() const {
        return goal_indices;
    }

    const std::vector<Transition> &get_transitions() const {
        return transitions;
    }

    void release_memory();
};
}

#endif
