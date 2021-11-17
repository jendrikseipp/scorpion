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
    State concrete_state;
    int abstract_state_id;
    Split desired_split;

    Flaw(State &&concrete_state,
         int abstract_state_id,
         Split&& desired_split);

    bool operator==(const Flaw &other) const {
        return concrete_state == other.concrete_state
               && abstract_state_id == other.abstract_state_id
               && desired_split == other.desired_split;
    }

    friend std::ostream &operator<<(std::ostream &os, const Flaw &f) {
        std::string split_values = "[";
        for (size_t i = 0; i < f.desired_split.values.size(); ++i)
        {
            if (i != 0)
                split_values += ", ";
            split_values += f.desired_split.values[i];
    }
    split_values += "]";
    return os << "[" << f.abstract_state_id << ","
              << f.concrete_state.get_id() << ","
              << "<" << f.desired_split.var_id << ","
              << split_values << ">]";
    }
};
}

#endif
