#ifndef COST_SATURATION_PARTIAL_STATE_TREE_H
#define COST_SATURATION_PARTIAL_STATE_TREE_H

#include "types.h"

#include "../task_proxy.h"

namespace cost_saturation {
class PartialStateTreeNode {
    int var_id;
    std::unique_ptr<std::vector<std::unique_ptr<PartialStateTreeNode>>> value_successors;
    std::unique_ptr<PartialStateTreeNode> ignore_successor;
public:
    PartialStateTreeNode();

    void add(
        const std::vector<FactPair> &partial_state,
        const std::vector<int> &domain_sizes,
        std::vector<int> &uncovered_vars);
    bool contains(const std::vector<FactPair> &partial_state) const;
    bool contains(const State &state) const;

    int get_num_nodes() const;
};

class PartialStateTree {
    int num_partial_states;
    PartialStateTreeNode root;
public:
    PartialStateTree();

    void add(
        const std::vector<FactPair> &partial_state,
        const std::vector<int> &domain_sizes);
    bool subsumes(const std::vector<FactPair> &partial_state) const;
    bool subsumes(const State &state) const;
    int size();
    int get_num_nodes() const;
};
}

#endif
