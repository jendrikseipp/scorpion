#ifndef CEGAR_FLAW_H
#define CEGAR_FLAW_H

#include "../algorithms/priority_queues.h"
#include "../utils/hash.h"

#include <utility>

class State;

namespace utils {
class RandomNumberGenerator;
}

namespace cegar {
struct FlawedState {
    int abs_id;
    int h;
    std::vector<State> concrete_states;

    FlawedState(int abs_id, int h, std::vector<State> &&concrete_states)
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
        return os << "abs_id: " << s.abs_id << ", h: " << s.h
                  << ", states: " << s.concrete_states.size();
    }

    static const FlawedState no_state;
};


class FlawedStates {
    utils::HashMap<int, std::vector<State>> flawed_states;
    priority_queues::AdaptiveQueue<int> flawed_states_queue;

    bool is_consistent() const;

public:
    void add_state(int abs_id, const State &conc_state, int h);
    FlawedState pop_flawed_state_with_min_h();
    FlawedState pop_random_flawed_state_and_clear(utils::RandomNumberGenerator &rng);
    void clear();
    bool empty() const;
    void dump() const;
};
}

#endif
