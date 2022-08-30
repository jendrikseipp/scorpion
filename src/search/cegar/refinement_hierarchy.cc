#include "refinement_hierarchy.h"

#include "utils.h"

#include "../task_proxy.h"

using namespace std;

namespace cegar {
Node::Node(int state_id)
    : left_child(UNDEFINED),
      right_child(UNDEFINED),
      var(UNDEFINED),
      value(state_id) {
    assert(!is_split());
}

bool Node::information_is_valid() const {
    bool not_split = (left_child == UNDEFINED && right_child == UNDEFINED &&
                      var == UNDEFINED);
    bool split = (left_child != UNDEFINED && right_child != UNDEFINED &&
                  var != UNDEFINED);
    return (not_split ^ split) && value != UNDEFINED;
}

bool Node::is_split() const {
    assert(information_is_valid());
    return left_child != UNDEFINED;
}

void Node::split(int var, int value, NodeID left_child, NodeID right_child) {
    this->var = var;
    this->value = value;
    this->left_child = left_child;
    this->right_child = right_child;
    assert(is_split());
}


ostream &operator<<(ostream &os, const Node &node) {
    if (node.is_split()) {
        return os << "<Leaf Node: state=" << node.value << ">";
    } else {
        return os << "<Inner Node: var=" << node.var << " value=" << node.value
                  << " left=" << node.left_child << " right=" << node.right_child << ">";
    }
}


RefinementHierarchy::RefinementHierarchy(const shared_ptr<AbstractTask> &task)
    : task(task) {
    nodes.emplace_back(0);
}

NodeID RefinementHierarchy::add_node(int state_id) {
    NodeID node_id = nodes.size();
    nodes.emplace_back(state_id);
    return node_id;
}

NodeID RefinementHierarchy::get_node_id(const State &state) const {
    NodeID id = 0;
    while (nodes[id].is_split()) {
        id = nodes[id].get_child(state[nodes[id].get_var()].get_value());
    }
    return id;
}

pair<NodeID, NodeID> RefinementHierarchy::split(
    NodeID node_id, int var, const vector<int> &values, int left_state_id, int right_state_id) {
    NodeID helper_id = node_id;
    NodeID right_child_id = add_node(right_state_id);
    for (int value : values) {
        NodeID new_helper_id = add_node(left_state_id);
        nodes[helper_id].split(var, value, new_helper_id, right_child_id);
        helper_id = new_helper_id;
    }
    return make_pair(helper_id, right_child_id);
}

int RefinementHierarchy::get_abstract_state_id(const State &state) const {
    TaskProxy subtask_proxy(*task);
    if (subtask_proxy.needs_to_convert_ancestor_state(state)) {
        State subtask_state = subtask_proxy.convert_ancestor_state(state);
        return nodes[get_node_id(subtask_state)].get_state_id();
    } else {
        return nodes[get_node_id(state)].get_state_id();
    }
}

int RefinementHierarchy::get_abstract_state_id(NodeID node_id) const {
    return nodes[node_id].get_state_id();
}

Children RefinementHierarchy::get_real_children(
    NodeID node_id, const CartesianSet &cartesian_set) const {
    const Node &node = nodes[node_id];
    assert(node.is_split());
    bool follow_right_child = cartesian_set.test(node.get_var(), node.value);
    // Traverse helper nodes.
    NodeID helper = node.left_child;
    while (nodes[helper].right_child == node.right_child) {
        if (!follow_right_child &&
            cartesian_set.test(nodes[helper].var, nodes[helper].value)) {
            follow_right_child = true;
        }
        helper = nodes[helper].left_child;
    }

    if (follow_right_child) {
        return {
                   node.right_child, helper
        };
    } else {
        return {
                   helper, node.right_child
        };
    }
}


TaskProxy RefinementHierarchy::get_task_proxy() const {
    return TaskProxy(*task);
}

shared_ptr<AbstractTask> RefinementHierarchy::get_task() const {
    return task;
}

void RefinementHierarchy::print_statistics() const {
    cout << "Refinement hierarchy nodes: " << nodes.size() << endl;
    cout << "Refinement hierarchy capacity: " << nodes.capacity() << endl;
    cout << "Refinement hierarchy estimated memory usage: "
         << estimate_memory_usage_in_bytes(nodes) / 1024 << " KB" << endl;
}

void cegar::RefinementHierarchy::dump(int level, NodeID id) const {
    for (int i = 0; i < level; ++i) {
        cout << "  ";
    }
    Node node = nodes[id];

    cout << id;
    if (node.is_split()) {
        cout << " (" << node.var << "=" << node.value << ")";
    }
    cout << endl;

    if (node.is_split()) {
        // Skip helper nodes.
        NodeID helper = node.left_child;
        while (nodes[helper].right_child == node.right_child) {
            helper = nodes[helper].left_child;
        }

        ++level;
        dump(level, helper);
        dump(level, nodes[id].right_child);
    }
}
}
