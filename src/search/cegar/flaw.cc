#include "flaw.h"

#include "../task_proxy.h"

#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
const FlawedState FlawedState::no_state = FlawedState(-1, -1, {});

bool FlawedStates::is_consistent() const {
    return flawed_states_queue.size() == static_cast<int>(flawed_states.size());
}

void FlawedStates::add_state(int abs_id, const State &conc_state, Cost h) {
    // Be careful not to add an entry while asserting that the state is not already present.
    // Using a reference to flawed_states[abs_id] doesn't work since it creates a temporary.
    assert(flawed_states.count(abs_id) == 0 || find(
               flawed_states.at(abs_id).begin(),
               flawed_states.at(abs_id).end(),
               conc_state.get_id()) == flawed_states.at(abs_id).end());
    flawed_states[abs_id].push_back(conc_state.get_id());
    // Note: we could probably avoid the second hash map lookup.
    if (flawed_states[abs_id].size() == 1) {
        // This is a new abstract state, add it to the queue.
        flawed_states_queue.push(h, abs_id);
    }
    // Assert that no bucket is empty.
    assert(none_of(flawed_states.begin(), flawed_states.end(),
                   [](const pair<const int, vector<StateID>> &pair) {
                       return pair.second.empty();
                   }));
    assert(is_consistent());
}

FlawedState FlawedStates::pop_flawed_state_with_min_h() {
    assert(!empty());
    auto pair = flawed_states_queue.pop();
    Cost old_h = pair.first;
    int abs_id = pair.second;
    vector<StateID> conc_states = move(flawed_states.at(abs_id));
    flawed_states.erase(abs_id);
    assert(is_consistent());
    return FlawedState(abs_id, old_h, move(conc_states));
}

FlawedState FlawedStates::pop_random_flawed_state_and_clear(utils::RandomNumberGenerator &rng) {
    auto random_bucket = next(flawed_states.begin(), rng.random(flawed_states.size()));
    int abstract_state_id = random_bucket->first;
    vector<StateID> conc_states = move(random_bucket->second);
    clear();
    assert(is_consistent());
    return FlawedState(abstract_state_id, -1, move(conc_states));
}

int FlawedStates::num_abstract_states() const {
    return flawed_states.size();
}

int FlawedStates::num_concrete_states(int abs_id) const {
    if (flawed_states.count(abs_id) == 0) {
        return 0;
    }
    return flawed_states.at(abs_id).size();
}

void FlawedStates::clear() {
    flawed_states.clear();
    flawed_states_queue.clear();
}

bool FlawedStates::empty() const {
    assert(is_consistent());
    return flawed_states.empty();
}

void FlawedStates::dump() const {
    int num_concrete_states = 0;
    for (auto pair : flawed_states) {
        num_concrete_states += pair.second.size();
    }

    cout << "Found " << num_concrete_states << " concrete states in "
         << flawed_states.size() << " abstract states." << endl;
    for (auto pair : flawed_states) {
        cout << "  id: " << pair.first << ", states: " << pair.second.size() << endl;
    }
}
}
