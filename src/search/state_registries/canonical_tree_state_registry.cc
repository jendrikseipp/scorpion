#include "canonical_tree_state_registry.h"
#include "../per_state_information.h"
#include "../task_proxy.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../tasks/root_task.h"
#include <iostream>

namespace vs = valla;
namespace vsf = valla::canonical_fixed_tree;

constexpr auto binary_merge_strategy = [](auto a, auto b) {
            // prefers powers of two, as they can be neatly represented by the canonical tree
            const auto is_pow2 = [](size_t n) {return (n & (n - 1)) == 0;};
            if (is_pow2(a->cost) && !is_pow2(b->cost)) {
                return false;
            }
            if (a->is_leaf && !b->is_leaf) {
                return false;
            }
            if (a->cost == b->cost) {
                return a->idx > b->idx;
            }
            return a->cost > b->cost;
};

// --- CanonicalTreeStateRegistry Implementation ---
CanonicalTreeStateRegistry::CanonicalTreeStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy)
    , state_packer(task_properties::g_state_packers[task_proxy])
    , axiom_evaluator(g_axiom_evaluators[task_proxy])
    , num_variables(task_proxy.get_variables().size())
    , merge_schedule_(vs::compute_merge_schedule(
        get_domain_sizes(task_proxy),binary_merge_strategy))
    , tree_size(0)
    , stored_traversals()
    , tree_table(cap, vs::Hasher(), vs::SlotEqual())
    , root_table(cap, vs::Hasher(), vs::SlotEqual())
    , traversal_repo(stored_traversals)    // Construct BitsetRepository with the pool
{
    utils::g_log << "CanonicalTreeStateRegistry::Traversal_Bits" << std::endl;
    for (size_t i = 0; i < merge_schedule_.traversal.size(); ++i)
        utils::g_log << merge_schedule_.traversal[i];
    utils::g_log << std::endl;

    utils::g_log << "CanonicalTreeStateRegistry::Variable_Order" << std::endl;
    utils::g_log << "Reordered variables: ";
    for (size_t i = 0; i < merge_schedule_.variable_order.size(); ++i) {
        auto var_name = tasks::g_root_task->get_variable_name(i);
        auto order = merge_schedule_.variable_order[i];
        if (var_name.size() > 3 && std::stoi(var_name.substr(3)) != order)
            utils::g_log << var_name << " -> " << order
                << "[" << task_proxy.get_variables()[order].get_domain_size() << "]" << " | ";
    }
    utils::g_log << std::endl;

    // Canonical reorder root task
    std::vector<unsigned int> var_order(
        merge_schedule_.variable_order.begin(),
        merge_schedule_.variable_order.end()
    );
    tasks::g_root_task->reorder(var_order);

    // Set variable value accessor for State
    State::get_variable_value = [this] (const StateID& id) -> std::vector<int> {
        auto [state_index, traversal_index] = root_table.get(id.get_value());
        std::vector<vs::Index> state_data(num_variables);

        vsf::read_state(
            state_index, traversal_repo[traversal_index], num_variables,
            merge_schedule_.traversal_splits, tree_table, state_data);

        return {state_data.begin(), state_data.end()};
    };
}

std::vector<size_t>
CanonicalTreeStateRegistry::get_domain_sizes(const TaskProxy &task_proxy) const {
    std::vector<size_t> domain_sizes;
    domain_sizes.reserve(num_variables);
    for (const auto &var : task_proxy.get_variables())
        domain_sizes.push_back(var.get_domain_size());
    return domain_sizes;
}

// --- Insert/lookup utilities ---

// Canonically insert a state, storing traversal bitset as needed, return StateID
StateID CanonicalTreeStateRegistry::insert_state(const std::vector<vs::Index>& state_vec) {
    // Insert canonically into tree (and deduplicate traversal bitset via repo)
    auto [slot_struct, _] = vsf::insert(
        state_vec,
        merge_schedule_.traversal_splits,
        tree_table,
        stored_traversals,
        traversal_repo
    );
    auto [root_idx, __] = root_table.insert(slot_struct);
    return StateID(root_idx);
}

// --- Core interface functions ---

State CanonicalTreeStateRegistry::lookup_state(StateID id) const {
    std::vector<vs::Index> tmp(num_variables);

    auto [state_index, traversal_index] = root_table.get(id.get_value());

    vsf::read_state(state_index, traversal_repo[traversal_index], num_variables,
                    merge_schedule_.traversal_splits, tree_table, tmp);

    std::vector<int> state_values{tmp.begin(), tmp.end()};
    return task_proxy.create_state(*this, id, std::move(state_values));
}

State CanonicalTreeStateRegistry::lookup_state(
    StateID id, std::vector<int> &&state_values) const {
    return task_proxy.create_state(*this, id, std::move(state_values));
}

const State &CanonicalTreeStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        State initial_state = task_proxy.get_initial_state();
        const auto &tmp = initial_state.get_unpacked_values();
        std::vector<vs::Index> state(tmp.begin(), tmp.end());
        StateID id = insert_state(state);
        cached_initial_state = std::make_unique<State>(lookup_state(id));
        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

State CanonicalTreeStateRegistry::get_successor_state(
    const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());
    predecessor.unpack();
    const auto& prev_values = predecessor.get_unpacked_values();

    std::vector<vs::Index> successor_values(prev_values.begin(), prev_values.end());
    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair e = effect.get_fact().get_pair();
            successor_values[e.var] = e.value;
        }
    }

    StateID result_id = insert_state(successor_values);
    return lookup_state(result_id, {successor_values.begin(), successor_values.end()});
}

int CanonicalTreeStateRegistry::get_state_size_in_bytes() const {
    return num_variables * sizeof(unsigned);
}

int CanonicalTreeStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}

void CanonicalTreeStateRegistry::print_statistics(utils::LogProxy &log) const {
    log << "Number of registered states: " << size() << std::endl;
    log << "Closed list load factor: " << tree_table.size() << std::endl;
    log << "State size in bytes: " << get_state_size_in_bytes() << std::endl;
    log << "State set size: " << tree_table.get_memory_usage() / 1024 << " KB" << std::endl;
    log << "Occupied State set size: " << tree_table.get_occupied_memory_usage() / 1024 << " KB" << std::endl;
    log << "Root set size: " << root_table.get_memory_usage() / 1024 << " KB" << std::endl;
    log << "Occupied Root set size: " << root_table.get_occupied_memory_usage() / 1024 << " KB" << std::endl;
    log << "Traversal pool size: " << stored_traversals.estimate_memory_usage() / 1024 << " KB" << std::endl;
    log << "Traversal repository size: " << traversal_repo.estimate_memory_usage() / 1024 << " KB" << std::endl;
}