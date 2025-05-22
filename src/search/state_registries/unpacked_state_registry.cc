#include "unpacked_state_registry.h"

#include "../per_state_information.h"
#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <iostream>

using namespace std;

// constexpr int unpacked_state_variable_reader(const unsigned *buf, int var, void*) noexcept {
//     return buf[var];
// }

UnpackedStateRegistry::UnpackedStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy), state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()),
      state_data_pool(num_variables),
      registered_states(
          0,
          StateIDSemanticHash(state_data_pool, num_variables),
          StateIDSemanticEqual(state_data_pool, num_variables)) {

    State::get_variable_value =
        [this](const StateID& id) {
            std::vector<int> state_data(get_bins_per_state());
            const unsigned *buffer = state_data_pool[id.value];
            for (int i = 0; i < num_variables; ++i) {
                state_data[i] = buffer[i];
            }
            return std::move(state_data);
    };
}

StateID UnpackedStateRegistry::insert_id_or_pop_state() {
    /*
      Attempt to insert a StateID for the last state of state_data_pool
      if none is present yet. If this fails (another entry for this state
      is present), we have to remove the duplicate entry from the
      state data pool.
    */
    StateID id(state_data_pool.size() - 1);
    auto result = registered_states.insert(id.value);
    bool is_new_entry = result.second;
    if (!is_new_entry) {
        state_data_pool.pop_back();
    }
    assert(registered_states.size() == state_data_pool.size());
    return StateID(*result.first);
}

State UnpackedStateRegistry::lookup_state(StateID id) const {
    const PackedStateBin *buffer = state_data_pool[id.value];

    std::vector<int> values(num_variables);
    for (int i = 0; i < num_variables; ++i) {
        values[i] = buffer[i];
    }
    return task_proxy.create_state(*this, id, std::move(values));
}

State UnpackedStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const {
    return task_proxy.create_state(*this, id, std::move(state_values));
}


const State &UnpackedStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        unique_ptr<unsigned[]> buffer(new unsigned[num_variables]);
        // Avoid garbage values in half-full bins.
        fill_n(buffer.get(), num_variables, 0);

        State initial_state = task_proxy.get_initial_state();
        for (size_t i = 0; i < initial_state.size(); ++i) {
            buffer[i] = initial_state[i].get_value();
        }
        state_data_pool.push_back(buffer.get());
        StateID id = insert_id_or_pop_state();
        cached_initial_state = make_unique<State>(lookup_state(id));

        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

//TODO it would be nice to move the actual state creation (and operator application)
//     out of the PackedStateRegistry. This could for example be done by global functions
//     operating on state buffers (unsigned *).
State UnpackedStateRegistry::get_successor_state(const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());

    std::vector<unsigned> state_values;

    predecessor.unpack();
    const std::vector<int> &predecessor_values = predecessor.get_unpacked_values();
    state_data_pool.push_back(reinterpret_cast<const unsigned *>(predecessor_values.data()));

    unsigned *buffer = state_data_pool[state_data_pool.size() - 1];
    /* Experiments for issue348 showed that for tasks with axioms it's faster
       to compute successor states using unpacked data. */
    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair effect_pair = effect.get_fact().get_pair();
            buffer[effect_pair.var] = effect_pair.value;
        }
        /*
          NOTE: insert_id_or_pop_state possibly invalidates buffer, hence
          we use lookup_state to retrieve the state using the correct buffer.
        */
        StateID id = insert_id_or_pop_state();
        return lookup_state(id);
    }
}

int UnpackedStateRegistry::get_state_size_in_bytes() const {
    return num_variables * sizeof(unsigned);
}

int UnpackedStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}
void UnpackedStateRegistry::print_statistics(utils::LogProxy &log) const {

    log << "Number of registered states: " << size() << endl;
    log << "Closed list load factor: " << registered_states.size()
        << "/" << registered_states.capacity() << " = "
        << registered_states.load_factor() << endl;
}