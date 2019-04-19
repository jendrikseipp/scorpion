#include "partial_state_tree.h"

#include "../utils/memory.h"

using namespace std;

namespace pdbs {
class PartialStateTreeNode {
public:
    virtual ~PartialStateTreeNode() = default;
    virtual void add(
        const vector<FactPair> &partial_state,
        const vector<int> &domain_sizes,
        int index) = 0;
    virtual bool contains(const std::vector<FactPair> &partial_state, int index) const = 0;
    virtual bool contains(const State &state) const = 0;
};


class PartialStateTreeLeafNode : public PartialStateTreeNode {
public:
    virtual void add(
        const vector<FactPair> &, const vector<int> &, int) override {
    }

    virtual bool contains(const std::vector<FactPair> &, int) const override {
        return true;
    }

    virtual bool contains(const State &) const override {
        return true;
    }
};


class PartialStateTreeSwitchNode : public PartialStateTreeNode {
    int var_id;
    vector<PartialStateTreeNode *> value_successors;
    PartialStateTreeNode *ignore_successor;
public:
    PartialStateTreeSwitchNode(int var, int domain_size)
        : var_id(var),
          value_successors(domain_size, nullptr),
          ignore_successor(nullptr) {
    }

    virtual ~PartialStateTreeSwitchNode() override {
        for (PartialStateTreeNode *child : value_successors)
            delete child;
        delete ignore_successor;
    }

    virtual void add(
        const vector<FactPair> &partial_state,
        const vector<int> &domain_sizes,
        int index) override {
        const FactPair &current_fact = partial_state[index];
        int current_var = current_fact.var;
        int current_value = current_fact.value;
        PartialStateTreeNode **successor;
        int next_index = index;
        if (var_id == current_var) {
            successor = &value_successors[current_value];
            ++next_index;
        } else {
            successor = &ignore_successor;
        }

        if (*successor) {
            (*successor)->add(partial_state, domain_sizes, next_index);
        } else {
            if (next_index == static_cast<int>(partial_state.size())) {
                *successor = new PartialStateTreeLeafNode();
            } else {
                int next_var = partial_state[next_index].var;
                *successor = new PartialStateTreeSwitchNode(next_var, domain_sizes[next_var]);
                (*successor)->add(partial_state, domain_sizes, next_index);
            }
        }
    }

    virtual bool contains(const std::vector<FactPair> &partial_state, int index) const override {
        if (index == static_cast<int>(partial_state.size()))
            return false;
        const FactPair &current_fact = partial_state[index];
        int current_var_id = current_fact.var;
        int current_value = current_fact.value;
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

    virtual bool contains(const State &state) const override {
        PartialStateTreeNode *value_successor = value_successors[state[var_id].get_value()];
        if (value_successor && value_successor->contains(state))
            return true;
        if (ignore_successor && ignore_successor->contains(state))
            return true;
        return false;
    }
};


PartialStateTree::PartialStateTree()
    : num_partial_states(0),
      root(nullptr) {
}

PartialStateTree::~PartialStateTree() {
}

void PartialStateTree::add(
    const std::vector<FactPair> &partial_state, const vector<int> &domain_sizes) {
    assert(!partial_state.empty());
    if (!root) {
        int root_var = partial_state[0].var;
        root = utils::make_unique_ptr<PartialStateTreeSwitchNode>(
            root_var, domain_sizes[root_var]);
    }
    root->add(partial_state, domain_sizes, 0);
    ++num_partial_states;
}

bool PartialStateTree::subsumes(const std::vector<FactPair> &partial_state) const {
    return root && root->contains(partial_state, 0);
}

bool PartialStateTree::subsumes(const State &state) const {
    return root && root->contains(state);
}

int PartialStateTree::size() {
    return num_partial_states;
}
}
