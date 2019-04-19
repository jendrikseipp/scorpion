#include "partial_state_tree.h"

#include "pattern_database.h"
#include "pattern_generator.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/countdown_timer.h"
#include "../utils/logging.h"

#include <memory>

using namespace std;

namespace pdbs {
class PartialStateTreeNode {
public:
    virtual ~PartialStateTreeNode() = default;
    virtual void add(const std::vector<FactProxy> &partial_state, int index = 0) = 0;
    virtual bool contains(const std::vector<FactProxy> &partial_state, int index = 0) = 0;
    virtual bool contains(const State &state) = 0;
};

class PartialStateTreeLeafNode : public PartialStateTreeNode {
public:
    virtual void add(const std::vector<FactProxy> & /*partial_state*/, int /*index*/) override {
        // No need to add, this node already recognizes the dead end.
    }

    virtual bool contains(const std::vector<FactProxy> & /*partial_state*/, int /*index*/) override {
        return true;
    }

    virtual bool contains(const State & /*state*/) override {
        return true;
    }
};

class PartialStateTreeSwitchNode : public PartialStateTreeNode {
    int var_id;
    vector<PartialStateTreeNode *> value_successors;
    PartialStateTreeNode *ignore_successor;
public:
    PartialStateTreeSwitchNode(VariableProxy var)
        : var_id(var.get_id()),
          value_successors(var.get_domain_size(), nullptr),
          ignore_successor(nullptr) {
    }

    virtual ~PartialStateTreeSwitchNode() {
        for (PartialStateTreeNode *child : value_successors)
            delete child;
        delete ignore_successor;
    }

    virtual void add(const std::vector<FactProxy> &partial_state, int index = 0) override {
        const FactProxy &current_fact = partial_state[index];
        VariableProxy current_var = current_fact.get_variable();
        int current_value = current_fact.get_value();
        PartialStateTreeNode **successor;
        int next_index = index;
        if (var_id == current_var.get_id()) {
            successor = &value_successors[current_value];
            ++next_index;
        } else {
            successor = &ignore_successor;
        }

        if (*successor) {
            (*successor)->add(partial_state, next_index);
        } else {
            if (next_index == static_cast<int>(partial_state.size())) {
                *successor = new PartialStateTreeLeafNode();
            } else {
                VariableProxy next_var = partial_state[next_index].get_variable();
                *successor = new PartialStateTreeSwitchNode(next_var);
                (*successor)->add(partial_state, next_index);
            }
        }
    }

    virtual bool contains(const std::vector<FactProxy> &partial_state, int index = 0) override {
        if (index == static_cast<int>(partial_state.size()))
            return false;
        const FactProxy &current_fact = partial_state[index];
        int current_var_id = current_fact.get_variable().get_id();
        int current_value = current_fact.get_value();
        int next_index = index;
        if (var_id == current_var_id) {
            ++next_index;
            PartialStateTreeNode *value_successor = value_successors[current_value];
            if (value_successor && value_successor->contains(partial_state, next_index))
                return true;
        }
        if (ignore_successor && ignore_successor->contains(partial_state, next_index))
            return true;
        return false;
    }

    virtual bool contains(const State &state) override {
        PartialStateTreeNode *value_successor = value_successors[state[var_id].get_value()];
        if (value_successor && value_successor->contains(state))
            return true;
        if (ignore_successor && ignore_successor->contains(state))
            return true;
        return false;
    }
};

PartialStateTree::PartialStateTree()
    : num_dead_ends(0),
      root(nullptr) {
}

PartialStateTree::~PartialStateTree() {
    delete root;
}

void PartialStateTree::add(const std::vector<FactProxy> &dead) {
    assert(!dead.empty());
    if (!root) {
        root = new PartialStateTreeSwitchNode(dead[0].get_variable());
    }
    root->add(dead);
    ++num_dead_ends;
}

bool PartialStateTree::recognizes(const std::vector<FactProxy> &partial_state) const {
    return root && root->contains(partial_state);
}

bool PartialStateTree::recognizes(const State &state) const {
    if (root) {
        return root->contains(state);
    }
    return false;
}
}
