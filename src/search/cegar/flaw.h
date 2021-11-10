#ifndef CEGAR_FLAW_H
#define CEGAR_FLAW_H

#include "cartesian_set.h"
#include "../task_proxy.h"
#include "types.h"

namespace cegar {
class Abstraction;
class AbstractState;
struct Split;

enum class FlawReason {
    NOT_APPLICABLE,
    PATH_DEVIATION,
};

struct Flaw {
    // Last concrete and abstract state reached while tracing solution.
    State concrete_state;
    // Hypothetical Cartesian set we would have liked to reach.
    CartesianSet desired_cartesian_set;

    FlawReason flaw_reason;
    int abstract_state_id;
    int h_value; // h_value of abstract state

    Flaw(State &&concrete_state,
         CartesianSet &&desired_cartesian_set,
         FlawReason reason,
         int abstract_state_id = -1,
         int h_value = -1);

    std::vector<Split> get_possible_splits(
        const Abstraction &abstraction) const;

    bool operator==(const Flaw &other) const {
        return concrete_state == other.concrete_state
               && flaw_reason == other.flaw_reason
               && abstract_state_id == other.abstract_state_id
               && h_value == other.h_value
               && desired_cartesian_set == other.desired_cartesian_set;
    }

    friend std::ostream &operator<<(std::ostream &os, const Flaw &f) {
        std::string flaw_reason =
            f.flaw_reason == FlawReason::NOT_APPLICABLE ? "a" : "d";
        return os << "[" << f.abstract_state_id << ","
                  << f.concrete_state.get_id() << ","
                  << flaw_reason << "," << f.desired_cartesian_set
                  << ",h=" << f.h_value << "]";
    }
};
}

#endif
