#ifndef CEGAR_REFINEMENT_HIERARCHY_H
#define CEGAR_REFINEMENT_HIERARCHY_H

#include "abstract_state.h"
#include "types.h"

#include <cassert>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

class AbstractTask;
class State;

namespace cegar {
class Node;

struct Family {
    NodeID parent;
    NodeID correct_child;
    NodeID other_child;

    Family(NodeID parent, NodeID correct_child, NodeID other_child)
        : parent(parent), correct_child(correct_child), other_child(other_child) {
    }
};

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

    Family get_real_children(NodeID node_id, const CartesianSet &cartesian_set) const;

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

    template<typename Callback>
    void for_each_visited_node(const AbstractState &state, const Callback &callback) const;

    template<typename Callback>
    void for_each_visited_family(const AbstractState &state, const Callback &callback) const;

    template<typename Callback>
    void for_each_leaf(
        const CartesianSets &all_cartesian_sets,
        const CartesianSet &cartesian_set, const Callback &callback, NodeID node_id = 0) const;

    void dump(int level = 0, NodeID id = 0) const;
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

    /* Before splitting the corresponding state for var and value, both
       members hold UNDEFINED. */
    int var;
    int value;

    // When splitting the corresponding state, we change this value to UNDEFINED.
    int state_id;

    bool information_is_valid() const;

public:
    explicit Node(int state_id);

    bool is_split() const;

    void split(int var, int value, NodeID left_child, NodeID right_child);

    int get_var() const {
        assert(is_split());
        return var;
    }

    NodeID get_child(int value) const {
        assert(is_split());
        if (value == this->value)
            return right_child;
        return left_child;
    }

    NodeID get_child(const AbstractState &state) const {
        assert(is_split());
        if (state.contains(var, value))
            return right_child;
        return left_child;
    }

    int get_state_id() const {
        return state_id;
    }

    friend std::ostream &operator<<(std::ostream &os, const Node &node);
};

// Invoke the callback function on normal *and* helper nodes.
template<typename Callback>
void RefinementHierarchy::for_each_visited_node(
    const AbstractState &state, const Callback &callback) const {
    NodeID state_node_id = state.get_node_id();
    NodeID node_id = 0;
    while (true) {
        callback(node_id);
        if (node_id == state_node_id) {
            break;
        }

        Family family = get_real_children(node_id, state.get_cartesian_set());
        node_id = family.correct_child;
    }
}

template<typename Callback>
void RefinementHierarchy::for_each_visited_family(
    const AbstractState &state, const Callback &callback) const {
    NodeID node_id = 0;
    while (nodes[node_id].is_split()) {
        Family family = get_real_children(node_id, state.get_cartesian_set());
        callback(family);
        node_id = family.correct_child;
    }
}

template<typename Callback>
void RefinementHierarchy::for_each_leaf(
    const CartesianSets &all_cartesian_sets, const CartesianSet &cartesian_set,
    const Callback &callback, NodeID node_id) const {
    // TODO: turn into while-loop.
    Node node = nodes[node_id];
    if (node.is_split()) {
        bool intersects_with_right_child = cartesian_set.test(node.var, node.value);
        // Traverse helper nodes.
        NodeID helper = node.left_child;
        while (nodes[helper].right_child == node.right_child) {
            if (!intersects_with_right_child &&
                cartesian_set.test(nodes[helper].var, nodes[helper].value)) {
                intersects_with_right_child = true;
            }
            helper = nodes[helper].left_child;
        }

        NodeID real_left_child = helper;

        // The Cartesian set must intersect with one or two of the children.
        bool intersects_with_left_child = !intersects_with_right_child ||
            cartesian_set.intersects(*all_cartesian_sets[real_left_child], node.var);
        assert(intersects_with_left_child ==
               cartesian_set.intersects(*all_cartesian_sets[real_left_child], node.var));
        if (intersects_with_left_child) {
            for_each_leaf(all_cartesian_sets, cartesian_set, callback, real_left_child);
        }

        assert(intersects_with_right_child ==
               cartesian_set.intersects(*all_cartesian_sets[node.right_child], node.var));
        if (intersects_with_right_child) {
            for_each_leaf(all_cartesian_sets, cartesian_set, callback, node.right_child);
        }
    } else {
        callback(node_id);
    }
}
}

#endif
