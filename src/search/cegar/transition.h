#ifndef CEGAR_TRANSITION_H
#define CEGAR_TRANSITION_H

#include "types.h"

#include "../utils/hash.h"

#include <iostream>

namespace cegar {
struct Transition {
    int op_id;
    int target_id;

    Transition()
        : op_id(UNDEFINED),
          target_id(UNDEFINED) {
    }

    Transition(int op_id, int target_id)
        : op_id(op_id),
          target_id(target_id) {
    }

    bool is_defined() const {
        return op_id != UNDEFINED && target_id != UNDEFINED;
    }

    bool operator==(const Transition &other) const {
        return op_id == other.op_id && target_id == other.target_id;
    }

    bool operator<(const Transition &other) const {
        return op_id < other.op_id ||
               (op_id == other.op_id && target_id < other.target_id);
    }

    friend std::ostream &operator<<(std::ostream &os, const Transition &t) {
        return os << "[" << t.op_id << "," << t.target_id << "]";
    }
};
}

namespace utils {
inline void feed(HashState &hash_state, const cegar::Transition &tr) {
    feed(hash_state, tr.op_id);
    feed(hash_state, tr.target_id);
}
}

#endif
