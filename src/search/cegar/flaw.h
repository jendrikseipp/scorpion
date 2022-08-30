#ifndef CEGAR_FLAW_H
#define CEGAR_FLAW_H

#include "shortest_paths.h"
#include "types.h"

#include "../utils/hash.h"

#include <utility>

class State;
class StateID;

namespace utils {
class RandomNumberGenerator;
}

namespace cegar {
struct FlawedState {
    int abs_id;
    Cost h;
    std::vector<StateID> concrete_states;

    FlawedState(int abs_id, Cost h, std::vector<StateID> &&concrete_states)
        : abs_id(abs_id),
          h(h),
          concrete_states(move(concrete_states)) {
    }

    bool operator==(const FlawedState &other) const {
        return abs_id == other.abs_id
               && h == other.h
               && concrete_states == other.concrete_states;
    }

    bool operator!=(const FlawedState &other) const {
        return !(*this == other);
    }

    friend std::ostream &operator<<(std::ostream &os, const FlawedState &s) {
        return os << "Flawed abstract state: id=" << s.abs_id << ", h=" << s.h
                  << ", states=" << s.concrete_states.size();
    }

    static const FlawedState no_state;
};


class FlawedStates {
    utils::HashMap<int, std::vector<StateID>> flawed_states;
    HeapQueue flawed_states_queue;

    bool is_consistent() const;

public:
    void add_state(int abs_id, const State &conc_state, Cost h);
    FlawedState pop_flawed_state_with_min_h();
    FlawedState pop_random_flawed_state_and_clear(utils::RandomNumberGenerator &rng);
    int num_abstract_states() const;
    int num_concrete_states(int abs_id) const;
    void clear();
    bool empty() const;
    void dump() const;
};
}

#endif
