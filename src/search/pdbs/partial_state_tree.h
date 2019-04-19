#ifndef PDBS_PARTIAL_STATE_TREE_H
#define PDBS_PARTIAL_STATE_TREE_H

#include "types.h"

#include "../task_proxy.h"

namespace pdbs {
class PartialStateTreeNode;

class PartialStateTree {
    int num_partial_states;
    std::unique_ptr<PartialStateTreeNode> root;
public:
    PartialStateTree();
    ~PartialStateTree();

    void add(
        const std::vector<FactPair> &partial_state,
        const std::vector<int> &domain_sizes);
    bool subsumes(const std::vector<FactPair> &partial_state) const;
    bool subsumes(const State &state) const;
    int size();
};
}

#endif
