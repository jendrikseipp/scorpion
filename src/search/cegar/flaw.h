#ifndef CEGAR_FLAW_H
#define CEGAR_FLAW_H

#include "cartesian_set.h"

#include <utility>

class State;

namespace cegar {
class AbstractState;

struct Flaw {
    const AbstractState &abstract_state;
    const State &concrete_state;
    CartesianSet desired_cartesian_set;

    Flaw(const AbstractState &abstract_state,
         const State &concrete_state,
         CartesianSet &&desired_cartesian_set)
        : abstract_state(abstract_state),
          concrete_state(concrete_state),
          desired_cartesian_set(std::move(desired_cartesian_set)) {
    }
};
}

#endif
