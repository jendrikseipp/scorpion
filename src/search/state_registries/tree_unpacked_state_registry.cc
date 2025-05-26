#include "tree_unpacked_state_registry.h"

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

TreeUnpackedStateRegistry::TreeUnpackedStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy), state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()) {


    State::get_variable_value = [this](const StateID& id) {
            std::vector<vs::Index> state_data(num_variables);
            vs::static_tree::read_state(id.value, num_variables, tree_table, root_table, state_data);
            return std::vector<int>{state_data.begin(), state_data.end()};
    };

}

StateID TreeUnpackedStateRegistry::insert_id_or_pop_state() {
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

State TreeUnpackedStateRegistry::lookup_state(StateID id) const {
    std::vector<vs::Index> tmp(num_variables);
    vs::static_tree::read_state(id.value, num_variables, tree_table, root_table, tmp);
    std::vector<int> state_values{tmp.begin(), tmp.end()};
    return task_proxy.create_state(*this, id, move(state_values));
}

State TreeUnpackedStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const {
    return lookup_state(id);
}

const State &TreeUnpackedStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        State initial_state = task_proxy.get_initial_state();


        auto &tmp = initial_state.get_unpacked_values();
        auto [index, _] = vs::static_tree::insert(std::vector<vs::Index>{tmp.begin(), tmp.end()},
                                                  tree_table, root_table);
        StateID id = StateID(index);
        cached_initial_state = make_unique<State>(lookup_state(id));

        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

//TODO it would be nice to move the actual state creation (and operator application)
//     out of the PackedStateRegistry. This could for example be done by global functions
//     operating on state buffers (unsigned *).
State TreeUnpackedStateRegistry::get_successor_state(const State &predecessor, const OperatorProxy &op) {
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

    auto [index, _] = vs::static_tree::insert(new_state_values, tree_table, root_table);

    return lookup_state(StateID(index), {new_state_values.begin(), new_state_values.end()});
}

int TreeUnpackedStateRegistry::get_state_size_in_bytes() const {
    return num_variables * sizeof(unsigned);
}

int TreeUnpackedStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}
void TreeUnpackedStateRegistry::print_statistics(utils::LogProxy &log) const {

    log << "Number of registered states: " << size() << endl;
    log << "Closed list load factor: " << root_table.size() << endl;
}