#ifndef CARTESIAN_ABSTRACTIONS_TRANSITION_SYSTEM_H
#define CARTESIAN_ABSTRACTIONS_TRANSITION_SYSTEM_H

#include "transition_rewirer.h"
#include "types.h"

#include <vector>

struct FactPair;
class OperatorsProxy;

namespace utils {
class LogProxy;
}

namespace cartesian_abstractions {
/*
  Rewire transitions after each split.
*/
class TransitionSystem {
    TransitionRewirer rewirer;

    // Transitions from and to other abstract states.
    std::deque<Transitions> incoming;
    std::deque<Transitions> outgoing;

    // Store self-loops (operator indices) separately to save space.
    std::deque<Loops> loops;

    int num_non_loops;
    int num_loops;

    void enlarge_vectors_by_one();

    // Add self-loops to single abstract state in trivial abstraction.
    void add_loops_in_trivial_abstraction();

    void rewire_incoming_transitions(
        const Transitions &old_incoming, const AbstractStates &states,
        int v_id, const AbstractState &v1, const AbstractState &v2, int var);
    void rewire_outgoing_transitions(
        const Transitions &old_outgoing, const AbstractStates &states,
        int v_id, const AbstractState &v1, const AbstractState &v2, int var);
    void rewire_loops(
        const Loops &old_loops,
        const AbstractState &v1, const AbstractState &v2, int var);

public:
    explicit TransitionSystem(const OperatorsProxy &ops);

    // Update transition system after v has been split for var into v1 and v2.
    void rewire(
        const AbstractStates &states, int v_id,
        const AbstractState &v1, const AbstractState &v2, int var);

    const std::deque<Transitions> &get_incoming_transitions() const;
    const std::deque<Transitions> &get_outgoing_transitions() const;
    std::vector<bool> get_looping_operators() const;

    const std::vector<FactPair> &get_preconditions(int op_id) const;

    int get_num_states() const;
    int get_num_operators() const;
    int get_num_non_loops() const;
    int get_num_loops() const;

    void print_statistics(utils::LogProxy &log) const;
    void dump() const;
};
}

#endif
