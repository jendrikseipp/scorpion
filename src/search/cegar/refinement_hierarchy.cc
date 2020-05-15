#include "refinement_hierarchy.h"

#include "../task_proxy.h"

using namespace std;

namespace cegar {
Node::Node(int state_id)
    : left_child(UNDEFINED),
      right_child(UNDEFINED),
      var(UNDEFINED),
      value(UNDEFINED),
      state_id(state_id) {
    assert(state_id != UNDEFINED);
    assert(!is_split());
}

bool Node::information_is_valid() const {
    return (left_child == UNDEFINED && right_child == UNDEFINED &&
            var == UNDEFINED && value == UNDEFINED && state_id != UNDEFINED) ||
           (left_child != UNDEFINED && right_child != UNDEFINED &&
            var != UNDEFINED && value != UNDEFINED && state_id == UNDEFINED);
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
    state_id = UNDEFINED;
    assert(is_split());
}

Siblings Node::get_children(const AbstractState &state) const {
    // This method assumes that node is an ancestor of state.
    NodeID state_ancestor_id = left_child;
    NodeID other_node_id = right_child;
    if (state.contains(var, value)) {
        swap(state_ancestor_id, other_node_id);
    }
    return Siblings(state_ancestor_id, other_node_id);
}


ostream &operator<<(ostream &os, const Node &node) {
    return os << "<Node: var=" << node.var << " value=" << node.value
              << " state=" << node.state_id << " left=" << node.left_child
              << " right=" << node.right_child << ">";
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
        const Node &node = nodes[id];
        id = node.get_child(state[node.get_var()].get_value());
    }
    return id;
}

pair<NodeID, NodeID> RefinementHierarchy::split(
    NodeID node_id, int var, const vector<int> &values, int left_state_id, int right_state_id) {
    if (values.size() != 1) {
        ABORT("Not implemented");
    }
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
    State subtask_state = subtask_proxy.convert_ancestor_state(state);
    return nodes[get_node_id(subtask_state)].get_state_id();
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
        ++level;
        dump(level, nodes[id].left_child);
        dump(level, nodes[id].right_child);
    }
}
}
