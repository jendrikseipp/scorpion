#ifndef CEGAR_FLAW_H
#define CEGAR_FLAW_H

#include "split_selector.h"
#include "types.h"

namespace cegar {
struct Flaw {
    int abstract_state_id;
    Split desired_split;

    Flaw(int abstract_state_id, Split &&desired_split);

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
