#include "fixed_tree_unpacked_state_registry.h"

#include "../per_state_information.h"
#include "../task_proxy.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <vector>
#include <random>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>

using namespace std;

FixedTreeUnpackedStateRegistry::FixedTreeUnpackedStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy),
      state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size())
{
    // Provide a getter that returns in original order, but reads storage in shuffled order
    State::get_variable_value = [this](const StateID& id) {
        // Read stored in shuffled order:
        std::vector<vs::Index> state_data(num_variables);
        vst::read_state(id.get_value(), num_variables, tree_table, state_data);
        return std::vector<int>{state_data.begin(), state_data.end()};

    };
}

StateID FixedTreeUnpackedStateRegistry::insert_id_or_pop_state() {
    // TODO: Implement per original logic, here just stub as in original
    return StateID(0);
}

State FixedTreeUnpackedStateRegistry::lookup_state(StateID id) const {
    // Read in shuffled index order from storage
    std::vector<vs::Index> tmp(num_variables);
    vst::read_state(id.get_value(), num_variables, tree_table, tmp);
    std::vector<int> state_values{tmp.begin(), tmp.end()};
    return task_proxy.create_state(*this, id, std::move(state_values));
}

State FixedTreeUnpackedStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const
{
    // This implementation always reconstructs state from id.
    return lookup_state(id);
}

const State &FixedTreeUnpackedStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        State initial_state = task_proxy.get_initial_state();
        auto &tmp = initial_state.get_unpacked_values();
        auto [index, _] = vst::insert(std::vector<vs::Index>{tmp.begin(), tmp.end()},
                                                  tree_table);
        ++_registered_states;
        StateID id = StateID(index);
        cached_initial_state = make_unique<State>(lookup_state(id));
        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

State FixedTreeUnpackedStateRegistry::get_successor_state(
    const State &predecessor, const OperatorProxy &op)
{
    assert(!op.is_axiom());
    predecessor.unpack();
    const auto& prev_values = predecessor.get_unpacked_values();

    // Start from natural-order predecessor, apply effects, then shuffle for insertion
    std::vector<vs::Index> successor_values{prev_values.begin(), prev_values.end()};

    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair e = effect.get_fact().get_pair();
            successor_values[e.var] = e.value;
        }
    }

    if (task_properties::has_axioms(task_proxy))
        axiom_evaluator.evaluate(reinterpret_cast<std::vector<int> &>(successor_values));

    auto [index, exists] = vst::insert(successor_values, tree_table);
    _registered_states += !exists;
    return lookup_state(StateID(index), {successor_values.begin(), successor_values.end()});
}

int FixedTreeUnpackedStateRegistry::get_state_size_in_bytes() const {
    return num_variables * sizeof(unsigned);
}

int FixedTreeUnpackedStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}

void FixedTreeUnpackedStateRegistry::print_statistics(utils::LogProxy &log) const {
    log << "Number of registered states: " << size() << endl;
    log << "Closed list load factor: " << tree_table.size() << endl;
    log << "State size in bytes: " << get_state_size_in_bytes() << endl;
    log << "State set size: " << tree_table.get_memory_usage() / 1024 << " KB" << endl;
    log << "Occupied State set size: " << tree_table.get_occupied_memory_usage() / 1024 << " KB" << endl;
}