#include "huffman_tree_state_registry.h"

#include "../per_state_information.h"
#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include "../tasks/root_task.h"
#include <iostream>

using namespace std;

//constexpr std::vector<int> unpacked_state_variable_reader(const int index, void* context) noexcept {
//    auto root_table = reinterpret_cast<const vs::RootIndices *>(context);
//    std::vector<int> state_values;
//    vst::read_state(index, size, *root_table, state_values);
//    return std::move();
//}

HuffmanTreeStateRegistry::HuffmanTreeStateRegistry(const TaskProxy &task_proxy)
    : IStateRegistry(task_proxy), state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()),
      merge_schedule_(
          vsf::compute_merge_schedule(get_domain_sizes(task_proxy))){

    utils::g_log << "HuffmanTreeStateRegistry::Traversal_Bits" << std::endl;
    for (auto i = 0; i < merge_schedule_.traversal.size(); i++) {
        utils::g_log << merge_schedule_.traversal[i];
    }
    utils::g_log << std::endl;

    utils::g_log << "HuffmanTreeStateRegistry::Variable_Order" << std::endl;
    utils::g_log << "Reordered variables: ";
    for (auto i = 0; i < merge_schedule_.variable_order.size(); i++) {
        auto var_name = tasks::g_root_task->get_variable_name(i);
        auto order = merge_schedule_.variable_order[i];

        if (var_name.size() > 3 && std::stoi(var_name.substr(3)) != order)
            utils::g_log << var_name << " -> " << order
                << "[" << task_proxy.get_variables()[order].get_domain_size() << "]" << " | ";
    }
    utils::g_log << std::endl;

    std::vector<unsigned int> var_order(merge_schedule_.variable_order.begin(), merge_schedule_.variable_order.end());
    tasks::g_root_task->reorder(var_order);

    State::get_variable_value = [this](const StateID& id) {
        std::vector<vs::Index> state_data(num_variables);
        vsf::read_state(id.get_value(), num_variables, merge_schedule_.traversal_splits, tree_table, state_data);
        return std::vector<int>{state_data.begin(), state_data.end()};
    };

}

std::vector<size_t> HuffmanTreeStateRegistry::get_domain_sizes(const TaskProxy &task_proxy) const {
    std::vector<size_t> domain_sizes;
    domain_sizes.reserve(num_variables);
    for (const auto &var : task_proxy.get_variables()) {
        domain_sizes.push_back(var.get_domain_size());
    }
    return domain_sizes;
}

StateID HuffmanTreeStateRegistry::insert_id_or_pop_state() {
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

State HuffmanTreeStateRegistry::lookup_state(StateID id) const {
    // Read in shuffled index order from storage
    std::vector<vs::Index> tmp(num_variables);
    vsf::read_state(id.get_value(), num_variables, merge_schedule_.traversal_splits, tree_table, tmp);
    std::vector<int> state_values{tmp.begin(), tmp.end()};
    return task_proxy.create_state(*this, id, std::move(state_values));
}

State HuffmanTreeStateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const {
    return task_proxy.create_state(*this, id, move(state_values));
}

const State &HuffmanTreeStateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        State initial_state = task_proxy.get_initial_state();
        auto &tmp = initial_state.get_unpacked_values();
        auto state = std::vector<vs::Index>{tmp.begin(), tmp.end()};
        auto [index, _] = vsf::insert(state,merge_schedule_.traversal_splits, tree_table);
        StateID id = StateID(index);
        cached_initial_state = make_unique<State>(lookup_state(id));
        cached_initial_state->unpack();
    }
    return *cached_initial_state;
}

//TODO it would be nice to move the actual state creation (and operator application)
//     out of the PackedStateRegistry. This could for example be done by global functions
//     operating on state buffers (unsigned *).
State HuffmanTreeStateRegistry::get_successor_state(const State &predecessor, const OperatorProxy &op) {
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

    auto [index, _] = vsf::insert(successor_values, merge_schedule_.traversal_splits, tree_table);

    return lookup_state(StateID(index), {successor_values.begin(), successor_values.end()});
}

int HuffmanTreeStateRegistry::get_state_size_in_bytes() const {
    return num_variables * sizeof(unsigned);
}

int HuffmanTreeStateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}
void HuffmanTreeStateRegistry::print_statistics(utils::LogProxy &log) const {

    log << "Number of registered states: " << size() << endl;
    log << "Closed list load factor: " << tree_table.size() << endl;
    log << "State size in bytes: " << get_state_size_in_bytes() << endl;
    log << "State set size: " << tree_table.get_memory_usage() / 1024 << " KB" << endl;
    log << "Occupied State set size: " << tree_table.get_occupied_memory_usage() / 1024 << " KB" << endl;

}