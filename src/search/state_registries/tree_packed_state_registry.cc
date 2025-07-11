#include "tree_packed_state_registry.h"

#include "../per_state_information.h"
#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <iostream>

using namespace std;

//constexpr std::vector<int> unpacked_state_variable_reader(const int index, void* context) noexcept {
//    auto root_table = reinterpret_cast<const vs::RootIndices *>(context);
//    std::vector<int> state_values;
//    vs::static_tree::read_state(index, size, *root_table, state_values);
//    return std::move();
//}

TreePackedStateRegistry::TreePackedStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy), state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()) {


    State::get_variable_value = [this](const StateID& id) {
            std::vector<vs::Index> buffer(get_bins_per_state());
            vs::static_tree::read_state(id.value, get_bins_per_state(), tree_table, buffer);

            std::vector<int> state_data(num_variables);
            for (int i = 0; i < num_variables; ++i) {
                state_data[i] = state_packer.get(buffer.data(), i);
            }

            return std::vector<int>{state_data.begin(), state_data.end()};
    };

}

StateID TreePackedStateRegistry::insert_id_or_pop_state() {
    /*
      Attempt to insert a StateID for the last state of state_data_pool
      if none is present yet. If this fails (another entry for this state
      is present), we have to remove the duplicate entry from the
      state data pool.
    */
//    StateID id(state_data_pool.size() - 1);
//    auto result = registered_states.insert(id.value);
//    bool is_new_entry = result.second;
//    if (!is_new_entry) {
//        state_data_pool.pop_back();
//    }
//    assert(registered_states.size() == state_data_pool.size());
//    return StateID(*result.first);
    return StateID(0);
}

State TreePackedStateRegistry::lookup_state(StateID id) const {
    std::vector<vs::Index> buffer(get_bins_per_state());
    vs::static_tree::read_state(id.value, get_bins_per_state(), tree_table, buffer);

    std::vector<int> state_values(num_variables);
    for (int i = 0; i < num_variables; ++i) {
        state_values[i] = state_packer.get(buffer.data(), i);
    }


    return task_proxy.create_state(*this, id, move(state_values));
}

State TreePackedStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const {
    return task_proxy.create_state(*this, id, move(state_values));
}

const State &TreePackedStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        State initial_state = task_proxy.get_initial_state();


        std::vector<PackedStateBin> buffer(get_bins_per_state());
        auto &tmp = initial_state.get_unpacked_values();
        for (auto i = 0; i < num_variables; ++i) {
            state_packer.set(buffer.data(), i, tmp[i]);
        }
        auto [index, _] = vs::static_tree::insert(buffer, tree_table);
        ++_registered_states;
        StateID id = StateID(index);
        cached_initial_state = make_unique<State>(lookup_state(id));

        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

//TODO it would be nice to move the actual state creation (and operator application)
//     out of the PackedStateRegistry. This could for example be done by global functions
//     operating on state buffers (unsigned *).
State TreePackedStateRegistry::get_successor_state(const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());

    std::vector<unsigned> state_values;

    predecessor.unpack();
    auto& tmp = predecessor.get_unpacked_values();
    std::vector<vs::Index> new_state_values(tmp.begin(), tmp.end());

    /* Experiments for issue348 showed that for tasks with axioms it's faster
       to compute successor states using unpacked data. */

    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair effect_pair = effect.get_fact().get_pair();
            new_state_values[effect_pair.var] = effect_pair.value;
        }
    }

    if (task_properties::has_axioms(task_proxy))
        axiom_evaluator.evaluate(reinterpret_cast<std::vector<int> &>(new_state_values));

    std::vector<vs::Index> buffer(get_bins_per_state());
    for (auto i = 0; i < num_variables; ++i) {
        state_packer.set(buffer.data(), i, new_state_values[i]);
    }
    auto [index, exists] = vs::static_tree::insert(buffer, tree_table);
    _registered_states += !exists;

    return lookup_state(StateID(index), {new_state_values.begin(), new_state_values.end()});
}

int TreePackedStateRegistry::get_state_size_in_bytes() const {
    return get_bins_per_state() * sizeof(unsigned);
}

int TreePackedStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}
void TreePackedStateRegistry::print_statistics(utils::LogProxy &log) const {

    log << "Number of registered states: " << _registered_states << endl;
    log << "Closed list load factor: " << tree_table.size() << endl;
    log << "State size in bytes: " << get_state_size_in_bytes() << endl;
    log << "State set size: " << tree_table.get_memory_usage() / 1024 << " KB" << endl;

}