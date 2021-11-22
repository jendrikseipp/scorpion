#ifndef CEGAR_FLAW_H
#define CEGAR_FLAW_H

#include "cartesian_set.h"
#include "split_selector.h"
#include "types.h"

#include "../task_proxy.h"

namespace cegar {
class Abstraction;
class AbstractState;

struct Flaw {
    // Last concrete and abstract state reached while tracing solution.
    int abstract_state_id;
    Split desired_split;

    Flaw(int abstract_state_id,
         Split &&desired_split);

    bool operator==(const Flaw &other) const {
        return abstract_state_id == other.abstract_state_id
               && desired_split == other.desired_split;
    }

    friend std::ostream &operator<<(std::ostream &os, const Flaw &f) {
        return os << "[" << f.abstract_state_id << ","
                  << f.desired_split << "]";
    }
};
}

#endif
