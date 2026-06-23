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

    // Helper data structure that holds the result for get_real_children().
    struct Children {
        NodeID intersecting_child;
        NodeID possibly_intersecting_child;

        Children(NodeID correct_child, NodeID other_child)
            : intersecting_child(correct_child),
              possibly_intersecting_child(other_child) {
        }
    };

    /*
      Traverse the hierarchy past the helper nodes and return the two "actual"
      children under the given node, out of which one (intersecting_child) is
      guaranteed to intersect with cartesian_set.
    */
    Children get_real_children(
        NodeID node_id, const CartesianSet &cartesian_set) const {
        const Node &node = nodes[node_id];
        assert(node.is_split());
        bool follow_right_child =
            cartesian_set.test(node.get_var(), node.value);
        // Traverse helper nodes.
        NodeID helper = node.left_child;
        while (nodes[helper].right_child == node.right_child) {
            if (!follow_right_child &&
                cartesian_set.test(nodes[helper].var, nodes[helper].value)) {
                follow_right_child = true;
            }
            helper = nodes[helper].left_child;
        }

        return follow_right_child ? Children(node.right_child, helper)
                                  : Children(helper, node.right_child);
    }

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

    // Call callback for each leaf node that intersects with cartesian_set.
    template<typename Callback>
    void for_each_leaf(
        const CartesianSets &all_cartesian_sets,
        const CartesianSet &cartesian_set, const Matcher &matcher,
        const Callback &callback) const;

    TaskProxy get_task_proxy() const;
    std::shared_ptr<AbstractTask> get_task() const;

    void print_statistics(utils::LogProxy &log) const;
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
            stack.push(children.intersecting_child);
            // Now test the other child.
            int var = nodes[node_id].var;
            if ((matcher[var] != MatcherVariable::SINGLE_VALUE) &&
                (matcher[var] == MatcherVariable::FULL_DOMAIN ||
                 cartesian_set.intersects(
                     *all_cartesian_sets[children.possibly_intersecting_child],
                     var))) {
                stack.push(children.possibly_intersecting_child);
            }
        } else {
            callback(node_id);
        }
    }
}
}

#endif
