#ifndef CARTESIAN_ABSTRACTIONS_TRANSITION_REWIRER_H
#define CARTESIAN_ABSTRACTIONS_TRANSITION_REWIRER_H

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>

struct FactPair;
class OperatorsProxy;

namespace cartesian_abstractions {
class TransitionRewirer {
    const std::vector<std::vector<FactPair>> preconditions_by_operator;
    const std::vector<std::vector<FactPair>> postconditions_by_operator;

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;

    void rewire_incoming_transitions(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, int v_id, const AbstractState &v1,
        const AbstractState &v2, int var) const;
    void rewire_outgoing_transitions(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, int v_id, const AbstractState &v1,
        const AbstractState &v2, int var) const;

public:
    explicit TransitionRewirer(const OperatorsProxy &ops);

    void rewire_transitions(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, int v_id, const AbstractState &v1,
        const AbstractState &v2, int var) const;

    void rewire_loops(
        std::deque<Loops> &loops, std::deque<Transitions> &incoming,
        std::deque<Transitions> &outgoing, int v_id, const AbstractState &v1,
        const AbstractState &v2, int var) const;

    const std::vector<FactPair> &get_preconditions(int op_id) const {
        assert(utils::in_bounds(op_id, preconditions_by_operator));
        return preconditions_by_operator[op_id];
    }
    const std::vector<FactPair> &get_postconditions(int op_id) const {
        assert(utils::in_bounds(op_id, postconditions_by_operator));
        return postconditions_by_operator[op_id];
    }

    const std::vector<Facts> &get_preconditions() const {
        return preconditions_by_operator;
    }
    const std::vector<Facts> &get_postconditions() const {
        return postconditions_by_operator;
    }

    int get_num_operators() const;
};
}

#endif
