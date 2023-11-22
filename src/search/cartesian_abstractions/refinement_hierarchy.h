#ifndef CARTESIAN_ABSTRACTIONS_REFINEMENT_HIERARCHY_H
#define CARTESIAN_ABSTRACTIONS_REFINEMENT_HIERARCHY_H

#include "abstract_state.h"
#include "types.h"

#include <cassert>
#include <memory>
#include <ostream>
#include <stack>
#include <utility>
#include <vector>

class AbstractTask;
class State;

namespace cartesian_abstractions {
struct Children {
    NodeID correct_child;
    NodeID other_child;

    Children(NodeID correct_child, NodeID other_child)
        : correct_child(correct_child), other_child(other_child) {
    }
};

class Node {
    friend class RefinementHierarchy;

    /*
      While right_child is always the node of a (possibly split)
      abstract state, left_child may be a helper node. We add helper
      nodes to the hierarchy to allow for efficient lookup in case more
      than one fact is split off a state.
    */
    NodeID left_child;
    NodeID right_child;

    // This is the split variable for inner nodes and UNDEFINED for leaf nodes.
    int var;

    // This is the split value for inner nodes and the state ID for leaf nodes.
    int value;

    bool information_is_valid() const;

public:
    explicit Node(int state_id);

    bool is_split() const;

    void split(int var, int value, NodeID left_child, NodeID right_child);

    int get_var() const {
        assert(is_split());
        return var;
    }

    NodeID get_child(int val) const {
        assert(is_split());
        if (val == value)
            return right_child;
        return left_child;
    }

    int get_state_id() const {
        assert(!is_split());
        return value;
    }

    friend std::ostream &operator<<(std::ostream &os, const Node &node);
};

static_assert(sizeof(Node) == 16, "Node has unexpected size");

/*
  This class stores the refinement hierarchy of a Cartesian
  abstraction. The hierarchy forms a DAG with inner nodes for each
  split and leaf nodes for the abstract states.

  It is used for efficient lookup of abstract states during search.

  Inner nodes correspond to abstract states that have been split (or
  helper nodes, see below). Leaf nodes correspond to the current
  (unsplit) states in an abstraction. The use of helper nodes makes
  this structure a directed acyclic graph (instead of a tree).
*/
class RefinementHierarchy {
    std::shared_ptr<AbstractTask> task;
    std::vector<Node> nodes;

    NodeID add_node(int state_id);
    NodeID get_node_id(const State &state) const;

    Children get_real_children(NodeID node_id, const CartesianSet &cartesian_set) const;

public:
    explicit RefinementHierarchy(const std::shared_ptr<AbstractTask> &task);

    /*
      Update the split tree for the new split. Additionally to the left
      and right child nodes add |values|-1 helper nodes that all have
      the right child as their right child and the next helper node as
      their left child.
    */
    std::pair<NodeID, NodeID> split(
        NodeID node_id, int var, const std::vector<int> &values,
        int left_state_id, int right_state_id);

    int get_abstract_state_id(const State &state) const;
    int get_abstract_state_id(NodeID node_id) const;

    int get_num_nodes() const {
        return nodes.size();
    }

    template<typename Callback>
    void for_each_leaf(
        const CartesianSets &all_cartesian_sets, const CartesianSet &cartesian_set,
        const Matcher &matcher, const Callback &callback) const;

    TaskProxy get_task_proxy() const;
    std::shared_ptr<AbstractTask> get_task() const;

    void print_statistics() const;
    void dump(int level = 0, NodeID id = 0) const;
};

template<typename Callback>
void RefinementHierarchy::for_each_leaf(
    const CartesianSets &all_cartesian_sets, const CartesianSet &cartesian_set,
    const Matcher &matcher, const Callback &callback) const {
    std::stack<NodeID> stack;
    stack.push(0);
    while (!stack.empty()) {
        NodeID node_id = stack.top();
        stack.pop();
        if (nodes[node_id].is_split()) {
            Children children = get_real_children(node_id, cartesian_set);

            // The Cartesian set must intersect with one or two of the children.
            // We know that it intersects with "correct child".
            stack.push(children.correct_child);
            // Now test the other child.
            int var = nodes[node_id].var;
            if ((matcher[var] != Variable::SINGLE_VALUE) && (
                    matcher[var] == Variable::FULL_DOMAIN ||
                    cartesian_set.intersects(
                        *all_cartesian_sets[children.other_child], var))) {
                stack.push(children.other_child);
            }
        } else {
            callback(node_id);
        }
    }
}
}

#endif
