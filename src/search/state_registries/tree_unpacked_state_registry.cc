#include "tree_unpacked_state_registry.h"

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

namespace {
/// Utility to generate a shuffled vector of indices 0..n-1
template<std::integral Index = std::size_t,
         std::uniform_random_bit_generator F = std::mt19937>
[[nodiscard]]
inline std::vector<Index> shuffled_indices(Index n, F&& rng = F{std::random_device{}()}) {
    std::vector<Index> indices(n);
    for (Index i = 0; i < n; ++i) {
        indices[i] = i;
    }
    std::shuffle(indices.begin(), indices.end(), rng);
    return indices;
}
}

TreeUnpackedStateRegistry::TreeUnpackedStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy),
      state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()),
      shuffled_var_indices(shuffled_indices(num_variables)) // <-- CREATE SHUFFLED INDEX VECTOR
{
    // Compute the inverse permutation so we can reconstruct the original order
    inv_shuffled_var_indices.resize(num_variables);
    for (int i = 0; i < num_variables; ++i) {
        inv_shuffled_var_indices[shuffled_var_indices[i]] = i;
    }

    // Provide a getter that returns in original order, but reads storage in shuffled order
    State::get_variable_value = [this](const StateID& id) {
        // Read stored in shuffled order:
        std::vector<vs::Index> stored(num_variables);
        vs::static_tree::read_state(id.value, num_variables, tree_table, stored);

        // Revert to natural order
        std::vector<int> state_values(num_variables);
        for (int i = 0; i < num_variables; ++i)
            state_values[i] = stored[inv_shuffled_var_indices[i]];
        return state_values;
    };
}

StateID TreeUnpackedStateRegistry::insert_id_or_pop_state() {
    // TODO: Implement per original logic, here just stub as in original
    return StateID(0);
}

State TreeUnpackedStateRegistry::lookup_state(StateID id) const {
    // Read in shuffled index order from storage
    std::vector<vs::Index> stored(num_variables);
    vs::static_tree::read_state(id.value, num_variables, tree_table, stored);

    // Restore to natural order for external interface
    std::vector<int> state_values(num_variables);
    for (int i = 0; i < num_variables; ++i)
        state_values[i] = stored[inv_shuffled_var_indices[i]];

    return task_proxy.create_state(*this, id, std::move(state_values));
}

State TreeUnpackedStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const
{
    // This implementation always reconstructs state from id.
    return lookup_state(id);
}

const State &TreeUnpackedStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        State initial_state = task_proxy.get_initial_state();
        const auto& natural_order = initial_state.get_unpacked_values();

        // Permute initial state into shuffled order for insertion
        std::vector<vs::Index> shuffled_state(num_variables);
        for (int i = 0; i < num_variables; ++i)
            shuffled_state[i] = natural_order[shuffled_var_indices[i]];

        // Insert into tree & get new id
        auto [index, _] = vs::static_tree::insert(shuffled_state, tree_table);
        StateID id = StateID(index);

        // Create state in original (natural) order for user interface
        cached_initial_state = make_unique<State>(lookup_state(id));
        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

State TreeUnpackedStateRegistry::get_successor_state(
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

    // Permute to shuffled order for storage
    std::vector<vs::Index> shuffled_state(num_variables);
    for (int i = 0; i < num_variables; ++i)
        shuffled_state[i] = successor_values[shuffled_var_indices[i]];

    // Insert in shuffled order, as in storage
    auto [index, _] = vs::static_tree::insert(shuffled_state, tree_table);

    // External state is restored to natural order using lookup_state
    return lookup_state(StateID(index), {shuffled_state.begin(), shuffled_state.end()});
}

int TreeUnpackedStateRegistry::get_state_size_in_bytes() const {
    return num_variables * sizeof(unsigned);
}

int TreeUnpackedStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}

void TreeUnpackedStateRegistry::print_statistics(utils::LogProxy &log) const {
    log << "Number of registered states: " << size() << endl;
    log << "Closed list load factor: " << tree_table.size() << endl;
    log << "State size in bytes: " << get_state_size_in_bytes() << endl;
    log << "State set size: " << tree_table.get_memory_usage() / 1024 << " KB" << endl;
}