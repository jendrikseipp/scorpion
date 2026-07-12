#ifndef COST_SATURATION_SSCP_DAG_H
#define COST_SATURATION_SSCP_DAG_H

#include "types.h"
#include "utils.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace cost_saturation {
// Index of a node in the arena of its generator.
using NodeId = int;
constexpr NodeId NO_NODE = -1;

enum class NodeType : uint8_t {
    LOOKUP,
    MAX,
    SUM,
};

/*
  Node in the DAG that represents a structured saturated cost partitioning:
  leaves look up goal distances in tables, inner nodes maximize or sum over
  the values of their children. Nodes live in the arena of their generator;
  the children of max and sum nodes are a slice of the arena's shared
  children pool.
*/
struct SSCPNode {
    NodeType type;

    // Children slice of max and sum nodes (empty for lookup nodes).
    int64_t children_offset;
    int num_children;

    // Lookup table position (only used for lookup nodes).
    int abstraction_id;
    int lookup_table_id;
};

/*
  All DAG nodes of a generator with their children. Storing the children of
  all nodes in one shared pool avoids a heap-allocated vector per node.
*/
struct NodeArena {
    std::vector<SSCPNode> nodes;
    std::vector<NodeId> children_pool;

    const SSCPNode &operator[](NodeId node) const {
        return nodes[node];
    }

    int size() const {
        return nodes.size();
    }

    NodeId add_lookup_node(int abstraction_id, int lookup_table_id) {
        nodes.push_back(
            SSCPNode{
                NodeType::LOOKUP, 0, 0, abstraction_id, lookup_table_id});
        return nodes.size() - 1;
    }

    /* Children must already exist, so children ids are always smaller than
       the id of their parent and the arena order is topological. */
    NodeId add_compositional_node(
        NodeType type, const std::vector<NodeId> &children) {
        assert(type == NodeType::MAX || type == NodeType::SUM);
        assert(std::all_of(children.begin(), children.end(),
                           [&](NodeId child) {
                               return child < static_cast<int>(nodes.size());
                           }));
        int64_t offset = children_pool.size();
        children_pool.insert(
            children_pool.end(), children.begin(), children.end());
        nodes.push_back(
            SSCPNode{
                type, offset, static_cast<int>(children.size()), -1, -1});
        return nodes.size() - 1;
    }

    // Children of the given max or sum node.
    const NodeId *children_begin(NodeId node) const {
        return children_pool.data() + nodes[node].children_offset;
    }

    const NodeId *children_end(NodeId node) const {
        return children_begin(node) + nodes[node].num_children;
    }
};

/*
  Transparent hash and equality for deduplicating compositional nodes by
  their children ids, so the caches can store node ids instead of a copy
  of the children vector per entry.
*/
struct NodeChildrenHash {
    using is_transparent = void;

    const NodeArena *arena;

    size_t operator()(const std::vector<NodeId> &children) const {
        return hash_bytes(
            children.data(), children.size() * sizeof(NodeId),
            children.size());
    }

    size_t operator()(NodeId node) const {
        int num_children = (*arena)[node].num_children;
        return hash_bytes(
            arena->children_begin(node), num_children * sizeof(NodeId),
            num_children);
    }
};

struct NodeChildrenEqual {
    using is_transparent = void;

    const NodeArena *arena;

    bool operator()(NodeId node, const std::vector<NodeId> &children) const {
        return (*arena)[node].num_children ==
               static_cast<int>(children.size()) &&
               std::equal(
                   children.begin(), children.end(),
                   arena->children_begin(node));
    }

    bool operator()(const std::vector<NodeId> &children, NodeId node) const {
        return (*this)(node, children);
    }

    bool operator()(NodeId node1, NodeId node2) const {
        return (*arena)[node1].num_children ==
               (*arena)[node2].num_children &&
               std::equal(
                   arena->children_begin(node1), arena->children_end(node1),
                   arena->children_begin(node2));
    }
};

enum class InstructionType : uint8_t {
    MAX,
    SUM,
};

/*
  Flattened list of max/sum instructions. Instruction i has type types[i]
  and operates on the value ids in [id_offsets[i], id_offsets[i + 1]) of
  the shared ids buffer. Hard tasks create millions of instructions, so
  avoiding a heap-allocated vector per instruction saves a lot of memory.
*/
struct Instructions {
    std::vector<InstructionType> types;
    std::vector<int> id_offsets;
    std::vector<int> ids;

    int size() const {
        return types.size();
    }

    void reserve(int num_instructions, int64_t num_ids) {
        types.reserve(num_instructions);
        id_offsets.reserve(num_instructions + 1);
        id_offsets.push_back(0);
        ids.reserve(num_ids);
    }
};

struct UnsolvabilityInfo {
    int abstraction_id;
    std::vector<bool> unsolvable_states;
    bool useful;

    UnsolvabilityInfo(int abstraction_id, int num_abstract_states)
        : abstraction_id(abstraction_id),
          unsolvable_states(num_abstract_states, false),
          useful(false) {
    }
};

// Everything the structured SCP heuristic needs at evaluation time.
struct StructuredSCPOrder {
    AbstractionFunctions abs_functions;
    std::vector<UnsolvabilityInfo> unsolvability_infos;
    Instructions instructions;
    std::vector<std::vector<std::vector<int>>> lookup_tables;
};
}

#endif
