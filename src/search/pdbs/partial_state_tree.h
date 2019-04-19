#ifndef PDBS_PARTIAL_STATE_TREE_H
#define PDBS_PARTIAL_STATE_TREE_H

#include "types.h"

#include "../task_proxy.h"

namespace pdbs {
class PartialStateTreeNode;

class PartialStateTree {
    int num_dead_ends;
    PartialStateTreeNode *root;
public:
    PartialStateTree();
    ~PartialStateTree();

    void add(const std::vector<FactProxy> &dead);

    bool recognizes(const std::vector<FactProxy> &partial_state) const;
    bool recognizes(const State &state) const;

    int size() {
        return num_dead_ends;
    }
};
}

#endif
