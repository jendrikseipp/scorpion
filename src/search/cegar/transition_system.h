#ifndef CEGAR_TRANSITION_SYSTEM_H
#define CEGAR_TRANSITION_SYSTEM_H

#include "refinement_hierarchy.h"

#include "../utils/collections.h"

#include <limits>
#include <unordered_map>
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
    int num_states;
    const RefinementHierarchy refinement_hierarchy;
    std::unordered_map<Node *, int> node_to_state_id;
    std::vector<bool> operator_induces_self_loop;
    std::vector<Transition> transitions;
    std::vector<int> goal_indices;

public:
    explicit TransitionSystem(const Abstraction &abstraction);
    ~TransitionSystem() = default;

    int get_num_abstract_states() const {
        return num_states;
    }

    int get_abstract_state_index(const GlobalState &concrete_state) const {
        State local_state = task_proxy.convert_global_state(concrete_state);
        Node *node = refinement_hierarchy.get_node(local_state);
        return node_to_state_id.at(node);
    }

    bool is_dead_end(const GlobalState &concrete_state) const {
        State local_state = task_proxy.convert_global_state(concrete_state);
        Node *node = refinement_hierarchy.get_node(local_state);
        return node->get_h_value() == std::numeric_limits<int>::max();
    }

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
