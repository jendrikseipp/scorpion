#ifndef SEARCH_SPACE_H
#define SEARCH_SPACE_H

#include "operator_cost.h"
#include "per_state_information.h"
#include "search_node_info.h"

#include <vector>

#include "task_utils/successor_generator.h"

class OperatorProxy;
class State;
class TaskProxy;

namespace utils {
class LogProxy;
}

class SearchNode {
    State state;
    SearchNodeInfo &info;

    void update_parent(const SearchNode &parent_node,
                       const OperatorProxy &parent_op, int adjusted_cost);
public:
    SearchNode(const State &state, SearchNodeInfo &info);

    const State &get_state() const;

    bool is_new() const;
    bool is_open() const;
    bool is_closed() const;
    bool is_dead_end() const;

    int get_g() const;

    void open_initial();
    void open_new_node(const SearchNode &parent_node,
                       const OperatorProxy &parent_op, int adjusted_cost);
    void reopen_closed_node(const SearchNode &parent_node,
                            const OperatorProxy &parent_op, int adjusted_cost);
    void update_open_node_parent(const SearchNode &parent_node,
                                 const OperatorProxy &parent_op, int adjusted_cost);
    void update_closed_node_parent(const SearchNode &parent_node,
                                   const OperatorProxy &parent_op, int adjusted_cost);
    void close();
    void mark_as_dead_end();

    void dump(const TaskProxy &task_proxy, utils::LogProxy &log) const;
};


class SearchSpace {
    PerStateInformation<SearchNodeInfo> search_node_infos;

    std::shared_ptr<StateRegistry> state_registry;
    utils::LogProxy &log;
public:
    SearchSpace(std::shared_ptr<StateRegistry> &state_registry, utils::LogProxy &log);

    SearchNode get_node(const State &state);

    std::vector<State> trace_states(const State &goal_state) const;

    std::vector<OperatorID> trace_path(const TaskProxy &task_proxy, const successor_generator::SuccessorGenerator &successor_generator, const State &
                                       goal_state) const;

    void dump(const TaskProxy &task_proxy) const;
    void print_statistics() const;
};

#endif
