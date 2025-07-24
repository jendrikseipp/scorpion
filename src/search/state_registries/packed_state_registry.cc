#include "packed_state_registry.h"

#include "../per_state_information.h"
#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

using namespace std;

// int packed_state_variable_reader(const int index, void* context) {
//     auto* state_packer = static_cast<int_packer::IntPacker*>(context);
//     return state_packer->get(registered_states, var);
// }

PackedStateRegistry::PackedStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy), state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()),
      state_data_pool(get_bins_per_state()),
      registered_states(
          0,
          StateIDSemanticHash(state_data_pool, get_bins_per_state()),
          StateIDSemanticEqual(state_data_pool, get_bins_per_state())) {

    State::get_variable_value =
        [this](const StateID& id) {
            std::vector<int> state_data(num_variables);
            const unsigned *buffer = state_data_pool[id.value];
            for (int i = 0; i < num_variables; ++i) {
                state_data[i] = state_packer.get(buffer, i);
            }
            return state_data;
        };
}

StateID PackedStateRegistry::insert_id_or_pop_state() {
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

State PackedStateRegistry::lookup_state(StateID id) const {
    return task_proxy.create_state(*this, id);
}

State PackedStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const {
    return task_proxy.create_state(*this, id, std::move(state_values));
}

const State &PackedStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        int num_bins = get_bins_per_state();
        unique_ptr<PackedStateBin[]> buffer(new PackedStateBin[num_bins]);
        // Avoid garbage values in half-full bins.
        fill_n(buffer.get(), num_bins, 0);

        State initial_state = task_proxy.get_initial_state();
        for (size_t i = 0; i < initial_state.size(); ++i) {
            state_packer.set(buffer.get(), i, initial_state[i].get_value());
        }
        state_data_pool.push_back(buffer.get());
        StateID id = insert_id_or_pop_state();
        cached_initial_state = make_unique<State>(lookup_state(id));
    }
    return *cached_initial_state;
}

//TODO it would be nice to move the actual state creation (and operator application)
//     out of the PackedStateRegistry. This could for example be done by global functions
//     operating on state buffers (PackedStateBin *).
State PackedStateRegistry::get_successor_state(const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());
    /*
      TODO: ideally, we would not modify state_data_pool here and in
      insert_id_or_pop_state, but only at one place, to avoid errors like
      buffer becoming a dangling pointer. This used to be a bug before being
      fixed in https://issues.fast-downward.org/issue1115.
    */
    auto packed_pred = state_data_pool[predecessor.get_id().value];
    state_data_pool.push_back(packed_pred);
    PackedStateBin *buffer = state_data_pool[state_data_pool.size() - 1];

    /* Experiments for issue348 showed that for tasks with axioms it's faster
       to compute successor states using unpacked data. */
    if (task_properties::has_axioms(task_proxy)) {
        predecessor.unpack();
        vector<int> new_values = predecessor.get_unpacked_values();
        for (EffectProxy effect : op.get_effects()) {
            if (does_fire(effect, predecessor)) {
                FactPair effect_pair = effect.get_fact().get_pair();
                new_values[effect_pair.var] = effect_pair.value;
            }
        }
        axiom_evaluator.evaluate(new_values);
        for (size_t i = 0; i < new_values.size(); ++i) {
            state_packer.set(buffer, i, new_values[i]);
        }
        /*
          NOTE: insert_id_or_pop_state possibly invalidates buffer, hence
          we use lookup_state to retrieve the state using the correct buffer.
        */
        StateID id = insert_id_or_pop_state();
        return lookup_state(id, move(new_values));
    } else {
        for (EffectProxy effect : op.get_effects()) {
            if (does_fire(effect, predecessor)) {
                FactPair effect_pair = effect.get_fact().get_pair();
                state_packer.set(buffer, effect_pair.var, effect_pair.value);
            }
        }
        StateID id = insert_id_or_pop_state();
        return lookup_state(id);
    }
}

int PackedStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}

int PackedStateRegistry::get_state_size_in_bytes() const {
    return get_bins_per_state() * sizeof(PackedStateBin);
}

size_t PackedStateRegistry::get_memory_usage() const
{
    size_t usage = 0;

    usage += state_data_pool.capactity() * get_state_size_in_bytes();
    usage += registered_states.capacity() * (sizeof(int) + 1);

    return usage;
}

size_t PackedStateRegistry::get_occupied_memory_usage() const {
    size_t usage = 0;

    usage += state_data_pool.size() * get_state_size_in_bytes();
    usage += registered_states.size() * (sizeof(int) + 1);

    return usage;
}

void PackedStateRegistry::print_statistics(utils::LogProxy &log) const {
    log << "Number of registered states: " << size() << endl;
    log << "Closed list load factor: " << registered_states.size()
        << "/" << registered_states.capacity() << " = "
        << registered_states.load_factor() << endl;
    log << "State size in bytes: " << get_state_size_in_bytes() << endl;
    utils::g_log << "State set destroyed, size: " << size() << " entries"<< std::endl;
    utils::g_log << "State set destroyed, size per entry: " << get_bins_per_state() << " blocks"<< std::endl;
    utils::g_log << "State set destroyed, byte size: " << get_occupied_memory_usage() << "B" << std::endl;
    utils::g_log << "State set destroyed, byte capacity: " << get_memory_usage() << "B" << std::endl;
}
