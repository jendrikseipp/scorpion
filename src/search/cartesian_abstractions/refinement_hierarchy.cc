#include "refinement_hierarchy.h"

#include "../task_proxy.h"

using namespace std;

namespace cartesian_abstractions {
Node::Node(int state_id)
    : left_child(UNDEFINED),
      right_child(UNDEFINED),
      var(UNDEFINED),
      value(state_id) {
    assert(!is_split());
}

bool Node::information_is_valid() const {
    return value != UNDEFINED && (
        // leaf node
        (left_child == UNDEFINED && right_child == UNDEFINED && var == UNDEFINED) ||
        // inner node
        (left_child != UNDEFINED && right_child != UNDEFINED && var != UNDEFINED));
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

NodeID RefinementHierarchy::get_node_id(const vector<int> &state) const {
    NodeID id = 0;
    while (nodes[id].is_split()) {
        id = nodes[id].get_child(state[nodes[id].get_var()]);
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
        subtask_proxy.convert_ancestor_state_values(state, tmp_state_values);
        return nodes[get_node_id(tmp_state_values)].get_state_id();
    } else {
        return nodes[get_node_id(state)].get_state_id();
    }
}
}
