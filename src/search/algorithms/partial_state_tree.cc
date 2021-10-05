#include "partial_state_tree.h"

#include "../utils/memory.h"

using namespace std;

namespace partial_state_tree {
static const int DEAD_END_LEAF = -2;
static const int REGULAR_LEAF = -1;

PartialStateTreeNode::PartialStateTreeNode()
    : var_id(REGULAR_LEAF) {
}

void PartialStateTreeNode::add(
    const vector<FactPair> &partial_state,
    const vector<int> &domain_sizes,
    vector<int> &uncovered_vars) {
    assert(is_sorted(partial_state.begin(), partial_state.end()));
    if (uncovered_vars.empty()) {
        /*
          We already covered all variables of partial_state, but there is
          a subtree below the current node. This means we previously found more
          specific dead ends and are now considering a more general one.
          Cut the subtree by replacing the node with a dead-end leaf node.
        */
        var_id = DEAD_END_LEAF;
        value_successors = nullptr;
        ignore_successor = nullptr;
        return;
    }
    if (var_id == DEAD_END_LEAF) {
        /*
          We ended up in a dead-end leaf. This means we previously found a more
          general dead end and are now considering a more specific one.
          No need to add the more specific one.
        */
        return;
    }
    if (var_id == REGULAR_LEAF) {
        /*
          We ended up in a leaf but we still have variables to cover. Pick one
          of them and turn the current leaf into a node for this variable.
          We create the pointers to child nodes, but create the nodes on demand.
        */
        var_id = uncovered_vars.back();
        value_successors = utils::make_unique_ptr<vector<unique_ptr<PartialStateTreeNode>>>();
        value_successors->resize(domain_sizes[var_id]);
    }

    /*
      If we end up here, the node has a var_id of an actual variable. Now look
      for the right successor. If var_id is not mentioned in the partial state,
      we stick with the ignore_successor.
    */
    unique_ptr<PartialStateTreeNode> *successor = &ignore_successor;
    for (const FactPair &fact : partial_state) {
        if (fact.var == var_id) {
            successor = &(*value_successors)[fact.value];
            /*
              var_id is a variable of the partial state, remove it from uncovered
              since we will cover it in this step.
            */
            uncovered_vars.erase(
                remove(uncovered_vars.begin(), uncovered_vars.end(), fact.var),
                uncovered_vars.end());
            break;
        } else if (fact.var > var_id) {
            break;
        }
    }

    /*
      We found the correct successor, now make sure there is a tree node there.
      Since we generate nodes on demand, the successor might still be a nullptr.
    */
    if (!*successor) {
        *successor = utils::make_unique_ptr<PartialStateTreeNode>();
    }

    (*successor)->add(partial_state, domain_sizes, uncovered_vars);
}

bool PartialStateTreeNode::contains(const vector<FactPair> &partial_state) const {
    assert(is_sorted(partial_state.begin(), partial_state.end()));
    if (var_id == DEAD_END_LEAF) {
        return true;
    }
    if (var_id == REGULAR_LEAF) {
        return false;
    }

    // See if partial_state has a value for var_id.
    int value = -1;
    for (const FactPair &fact : partial_state) {
        if (fact.var == var_id) {
            value = fact.value;
            break;
        } else if (fact.var > var_id) {
            break;
        }
    }

    if (value != -1) {
        const auto &value_successor = (*value_successors)[value];
        if (value_successor && value_successor->contains(partial_state))
            return true;
    }
    return ignore_successor && ignore_successor->contains(partial_state);
}

bool PartialStateTreeNode::contains(const State &state) const {
    if (var_id == DEAD_END_LEAF) {
        return true;
    }
    if (var_id == REGULAR_LEAF) {
        return false;
    }

    const auto &value_successor = (*value_successors)[state[var_id].get_value()];
    return (value_successor && value_successor->contains(state)) ||
           (ignore_successor && ignore_successor->contains(state));
}

int PartialStateTreeNode::get_num_nodes() const {
    int num_nodes = 1;
    if (value_successors) {
        for (const unique_ptr<PartialStateTreeNode> &successor : *value_successors) {
            if (successor) {
                num_nodes += successor->get_num_nodes();
            }
        }
    }
    if (ignore_successor) {
        num_nodes += ignore_successor->get_num_nodes();
    }
    return num_nodes;
}


PartialStateTree::PartialStateTree()
    : num_partial_states(0) {
}

void PartialStateTree::add(
    const vector<FactPair> &partial_state, const vector<int> &domain_sizes) {
    vector<int> uncovered_vars;
    uncovered_vars.reserve(partial_state.size());
    for (const FactPair &fact : partial_state) {
        uncovered_vars.push_back(fact.var);
    }
    root.add(partial_state, domain_sizes, uncovered_vars);
    ++num_partial_states;
}

bool PartialStateTree::subsumes(const vector<FactPair> &partial_state) const {
    return root.contains(partial_state);
}

bool PartialStateTree::subsumes(const State &state) const {
    return root.contains(state);
}

int PartialStateTree::size() {
    return num_partial_states;
}

int PartialStateTree::get_num_nodes() const {
    return root.get_num_nodes();
}
}
