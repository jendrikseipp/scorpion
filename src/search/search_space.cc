#include "search_space.h"

#include "operator_id.h"
#include "search_node_info.h"
#include "task_proxy.h"

#include "task_utils/successor_generator.h"
#include "task_utils/task_properties.h"
#include "utils/logging.h"
#include "utils/system.h"

#include <cassert>

using namespace std;

SearchNode::SearchNode(const State &state, SearchNodeInfo &info)
    : state(state), info(info) {
    assert(state.get_id() != StateID::no_state);
}

const State &SearchNode::get_state() const {
    return state;
}

bool SearchNode::is_open() const {
    return info.status == SearchNodeInfo::OPEN;
}

bool SearchNode::is_closed() const {
    return info.status == SearchNodeInfo::CLOSED;
}

bool SearchNode::is_dead_end() const {
    return info.status == SearchNodeInfo::DEAD_END;
}

bool SearchNode::is_new() const {
    return info.status == SearchNodeInfo::NEW;
}

int SearchNode::get_g() const {
    assert(info.g >= 0);
    return info.g;
}

int SearchNode::get_real_g() const {
    return info.real_g;
}

void SearchNode::open_initial() {
    assert(info.status == SearchNodeInfo::NEW);
    info.status = SearchNodeInfo::OPEN;
    info.g = 0;
    info.real_g = 0;
    info.parent_state_id = StateID::no_state;
}

void SearchNode::update_parent(
    const SearchNode &parent_node, const OperatorProxy &parent_op,
    int adjusted_cost) {
    info.g = parent_node.info.g + adjusted_cost;
    info.real_g = parent_node.info.real_g + parent_op.get_cost();
    info.parent_state_id = parent_node.get_state().get_id();
}

void SearchNode::open_new_node(
    const SearchNode &parent_node, const OperatorProxy &parent_op,
    int adjusted_cost) {
    assert(info.status == SearchNodeInfo::NEW);
    info.status = SearchNodeInfo::OPEN;
    update_parent(parent_node, parent_op, adjusted_cost);
}

void SearchNode::reopen_closed_node(
    const SearchNode &parent_node, const OperatorProxy &parent_op,
    int adjusted_cost) {
    assert(info.status == SearchNodeInfo::CLOSED);
    info.status = SearchNodeInfo::OPEN;
    update_parent(parent_node, parent_op, adjusted_cost);
}

void SearchNode::update_open_node_parent(
    const SearchNode &parent_node, const OperatorProxy &parent_op,
    int adjusted_cost) {
    assert(info.status == SearchNodeInfo::OPEN);
    update_parent(parent_node, parent_op, adjusted_cost);
}

void SearchNode::update_closed_node_parent(
    const SearchNode &parent_node, const OperatorProxy &parent_op,
    int adjusted_cost) {
    assert(info.status == SearchNodeInfo::CLOSED);
    update_parent(parent_node, parent_op, adjusted_cost);
}

void SearchNode::close() {
    assert(info.status == SearchNodeInfo::OPEN);
    info.status = SearchNodeInfo::CLOSED;
}

void SearchNode::mark_as_dead_end() {
    info.status = SearchNodeInfo::DEAD_END;
}

void SearchNode::dump(const TaskProxy &, utils::LogProxy &log) const {
    if (log.is_at_least_debug()) {
        log << state.get_id() << ": ";
        task_properties::dump_fdr(state);
        if (info.parent_state_id != StateID::no_state)
            log << " has parent " << info.parent_state_id << endl;
        else
            log << " no parent" << endl;
    }
}

SearchSpace::SearchSpace(StateRegistry &state_registry, utils::LogProxy &log)
    : state_registry(state_registry), log(log) {
}

SearchNode SearchSpace::get_node(const State &state) {
    return SearchNode(state, search_node_infos[state]);
}

vector<OperatorID> SearchSpace::trace_path(
    const TaskProxy &task_proxy,
    const successor_generator::SuccessorGenerator &successor_generator,
    const State &goal_state) const {
    vector<OperatorID> path;
    vector<State> states = trace_states(goal_state);
    OperatorsProxy operators = task_proxy.get_operators();
    // Use the registry of one of the registered states.
    StateRegistry *registry =
        const_cast<StateRegistry *>(states[0].get_registry());
    // Recompute operator sequence between successive states by applicability.
    for (size_t i = 0; i + 1 < states.size(); ++i) {
        const State &s = states[i];
        const State &next = states[i + 1];
        vector<OperatorID> applicable_op_ids;
        successor_generator.generate_applicable_ops(s, applicable_op_ids);
        bool found = false;
        for (OperatorID op_id : applicable_op_ids) {
            State succ = registry->get_successor_state(s, operators[op_id]);
            if (succ == next) {
                path.push_back(op_id);
                found = true;
                break;
            }
        }
        if (!found) {
            cerr << "Internal error: couldn't recompute operator from state "
                 << s.get_id() << " to next state " << next.get_id() << "."
                 << endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
    }
    return path;
}

vector<State> SearchSpace::trace_states(const State &goal_state) const {
    vector<State> states;
    State current_state = goal_state;
    assert(current_state.get_registry() == &state_registry);
    // Collect states from goal to initial by following parent_state_id.
    for (;;) {
        const SearchNodeInfo &info = search_node_infos[current_state];
        states.push_back(current_state);
        if (info.parent_state_id == StateID::no_state) {
            break;
        }
        current_state = state_registry.lookup_state(info.parent_state_id);
    }
    // We collected in reverse; put in start->...->goal order.
    reverse(states.begin(), states.end());
    return states;
}

void SearchSpace::dump(const TaskProxy &task_proxy) const {
    OperatorsProxy operators = task_proxy.get_operators();
    for (StateID id : state_registry) {
        /* The body duplicates parts of SearchNode::dump() but we cannot create
           a search node without discarding the const qualifier. */
        State state = state_registry.lookup_state(id);
        const SearchNodeInfo &node_info = search_node_infos[state];
        log << id << ": ";
        task_properties::dump_fdr(state);
        if (node_info.parent_state_id != StateID::no_state)
            log << " has parent " << node_info.parent_state_id << endl;
        else
            log << "has no parent" << endl;
    }
}

void SearchSpace::print_statistics() const {
    state_registry.print_statistics(log);
}
