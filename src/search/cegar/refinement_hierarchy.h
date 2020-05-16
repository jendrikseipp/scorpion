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

struct Siblings {
    NodeID correct_child;
    NodeID other_child;

    Siblings(NodeID correct_child, NodeID other_child)
        : correct_child(correct_child), other_child(other_child) {
    }
};

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
    friend class MatchTree;

    std::shared_ptr<AbstractTask> task;
    std::vector<Node> nodes;

    NodeID add_node(int state_id);
    NodeID get_node_id(const State &state) const;

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
    friend class MatchTree;
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

    Siblings get_children(const AbstractState &state) const;

    int get_state_id() const {
        return state_id;
    }

    friend std::ostream &operator<<(std::ostream &os, const Node &node);
};

template<typename Callback>
void RefinementHierarchy::for_each_visited_node(
    const AbstractState &state, const Callback &callback) const {
    // TODO: ignore helper nodes.
    NodeID state_node_id = state.get_node_id();
    NodeID node_id = 0;
    while (true) {
        callback(node_id);
        if (node_id == state_node_id) {
            break;
        }
        node_id = nodes[node_id].get_child(state);
    }
}

template<typename Callback>
void RefinementHierarchy::for_each_visited_family(
    const AbstractState &state, const Callback &callback) const {
    // TODO: ignore helper nodes.
    NodeID node_id = 0;
    Node node = nodes[node_id];
    while (node.is_split()) {
        Siblings siblings = node.get_children(state);
        Family family(node_id, siblings.correct_child, siblings.other_child);
        callback(family);
        node_id = siblings.correct_child;
        node = nodes[node_id];
    }
}

template<typename Callback>
void RefinementHierarchy::for_each_leaf(
    const CartesianSets &all_cartesian_sets, const CartesianSet &cartesian_set, const Callback &callback, NodeID node_id) const {
    // TODO: ignore helper nodes.
    // TODO: turn into while-loop.
    Node node = nodes[node_id];
    if (node.is_split()) {
        // TODO: Can we use test() instead of intersects() here and in more places?
        if (cartesian_set.intersects(*all_cartesian_sets[node.left_child], node.var)) {
            for_each_leaf(all_cartesian_sets, cartesian_set, callback, node.left_child);
        }
        if (cartesian_set.intersects(*all_cartesian_sets[node.right_child], node.var)) {
            for_each_leaf(all_cartesian_sets, cartesian_set, callback, node.right_child);
        }
    } else {
        callback(node_id);
    }
}
}

#endif
