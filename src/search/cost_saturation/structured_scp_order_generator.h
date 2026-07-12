#ifndef COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_H
#define COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_H

#include "types.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../algorithms/connected_components.h"
#include "../utils/collections.h"
#include "../utils/logging.h"

#include "gtl/phmap.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace plugins {
class Feature;
class Options;
}

namespace cost_saturation {
using Costs = std::vector<int>;
using CostKey = uint32_t;
// Bit mask over the task's operators, one bit per operator.
using OpMask = std::vector<uint64_t>;
using NodeKey = std::pair<CostKey, std::vector<int>>;
using NodeKeyHash = PairUint32VectorIntHash;

struct StructuredSCPOptions {
    bool use_unsolvability_infos;
    bool use_general_cp;
    bool cache_lookup_tables;
    bool use_affecting_labels;
    bool use_non_negative_labels;
    bool use_infinite_labels;
    bool use_cost_partitioning_check;
    bool cache_scf_functions;
    int max_lookup_table_cache_resizes;
};

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
  the children of max and sum nodes are a slice of the generator's shared
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
        assert(all_of(children.begin(), children.end(),
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

struct StructuredSCPOrder {
    AbstractionFunctions abs_functions;
    std::vector<UnsolvabilityInfo> unsolvability_infos;
    Instructions instructions;
    std::vector<std::vector<std::vector<int>>> lookup_tables;
};

struct SaturatedCostFunction {
    Costs costs;
    /* Operators with non-zero saturated cost. Saturated cost functions are
       usually sparse, so loops over them only need to visit these
       operators. */
    std::vector<int> nonzero_ops;

    explicit SaturatedCostFunction(Costs &&_costs)
        : costs(move(_costs)) {
        for (size_t op_id = 0; op_id < costs.size(); ++op_id) {
            if (costs[op_id] != 0) {
                nonzero_ops.push_back(op_id);
            }
        }
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

/*
  Append-only pool of bit-packed cost functions, used to verify hash hits
  in the cost function registry. One shared buffer avoids a heap-allocated
  vector per cost function; hard tasks store hundreds of thousands.
*/
struct PackedCostsPool {
    std::vector<uint8_t> data;
    // Blob of cost function key k is data[offsets[k]..offsets[k + 1]).
    std::vector<int64_t> offsets = {0};

    int size() const {
        return offsets.size() - 1;
    }

    /* Pack the costs with the minimum number of bits per cost. Equal cost
       functions produce identical blobs, so blobs can be compared with
       memcmp(). */
    static void pack(const Costs &costs, std::vector<uint8_t> &blob);

    void append(const std::vector<uint8_t> &blob) {
        data.insert(data.end(), blob.begin(), blob.end());
        offsets.push_back(data.size());
    }

    bool equals(CostKey key, const std::vector<uint8_t> &blob) const {
        return offsets[key + 1] - offsets[key] ==
               static_cast<int64_t>(blob.size()) &&
               std::equal(blob.begin(), blob.end(), data.begin() + offsets[key]);
    }
};

/*
  A cost function together with a classification of its operators for the
  dependency checks: live operators always create a dependency between
  abstractions they affect; conditional operators (remaining cost 0) only
  create a dependency between abstractions that do not both guarantee
  nonincreasing remaining costs. The masks are maintained incrementally as
  costs are reduced along the DAG construction.
*/
struct CostContext {
    Costs costs;
    /* Key of the cost function in the generator's cost function registry
       and an order-independent content hash (sum of per-operator mixes)
       that is updated incrementally as costs are reduced. Both are only
       valid when the context was created or reduced by the generator;
       temporarily simulated costs leave them stale. */
    CostKey key;
    uint64_t cost_hash;
    OpMask live_ops;
    OpMask cond_ops;
};

class StructuredSCPOrderGenerator {
public:
    StructuredSCPOrderGenerator(
        const std::shared_ptr<AbstractTask> &transform,
        Abstractions abstractions, const StructuredSCPOptions &options,
        utils::Verbosity verbosity);
    virtual ~StructuredSCPOrderGenerator() = default;

    StructuredSCPOrder generate();

protected:
    Abstractions abstractions;
    std::vector<UnsolvabilityInfo> unsolvability_infos;
    const TaskProxy task_proxy;
    const StructuredSCPOptions options;
    bool precomputed_conflicting_ops;
    int recomputed_lookup_tables;
    int lookup_cache_hits;
    // Maximum number of cost keys cached in lookup_tables_cache.
    CostKey max_lookup_table_entries;
    utils::LogProxy log;
    /* Cost functions are identified by the 64-bit hash of their raw
       vector and verified against a packed copy, which shrinks the stored
       cost functions by 4x; hard tasks register hundreds of thousands of
       them. Hash collisions land in the (in practice empty) overflow
       list. */
    gtl::flat_hash_map<uint64_t, CostKey> cost_key_by_hash;
    std::vector<std::pair<uint64_t, CostKey>> cost_key_overflow;
    // Packed cost function per cost key, for verification.
    PackedCostsPool packed_costs;
    // Scratch blob for lookup_costs_or_register().
    std::vector<uint8_t> packed_costs_scratch;
    /* Caches per (cost key, abstraction) the lookup table id for
       evaluating the abstraction under the cost function (UNKNOWN_LOOKUP
       if not computed yet, PRUNED_LOOKUP if pruned). Rows are created on
       the first lookup with a cost key and store 16-bit entries to halve
       the dominant cache; the rare table ids that do not fit (very large
       tasks) go to an overflow map, so all id ranges are supported. */
    class LookupTableCache {
        std::vector<std::vector<int16_t>> rows;
        gtl::flat_hash_map<uint64_t, int> overflow;
        int num_abstractions = 0;

        static const int16_t SMALL_UNKNOWN = -2;
        static const int16_t SMALL_OVERFLOW = -3;

        static uint64_t overflow_key(CostKey cost_key, int abstraction_id) {
            return (static_cast<uint64_t>(cost_key) << 32) | abstraction_id;
        }

    public:
        void initialize(int num_abstractions) {
            this->num_abstractions = num_abstractions;
        }

        int64_t size() const {
            return static_cast<int64_t>(rows.size()) * num_abstractions;
        }

        // Returns the table id, PRUNED_LOOKUP or UNKNOWN_LOOKUP.
        int get(CostKey cost_key, int abstraction_id) {
            if (cost_key >= rows.size()) {
                rows.resize(cost_key + 1);
            }
            std::vector<int16_t> &row = rows[cost_key];
            if (row.empty()) {
                row.assign(num_abstractions, SMALL_UNKNOWN);
            }
            int16_t entry = row[abstraction_id];
            if (entry == SMALL_OVERFLOW) {
                return overflow.at(overflow_key(cost_key, abstraction_id));
            }
            return entry;
        }

        // table_id is a valid table id or PRUNED_LOOKUP.
        void set(CostKey cost_key, int abstraction_id, int table_id) {
            if (table_id > std::numeric_limits<int16_t>::max()) {
                rows[cost_key][abstraction_id] = SMALL_OVERFLOW;
                overflow[overflow_key(cost_key, abstraction_id)] = table_id;
            } else {
                rows[cost_key][abstraction_id] =
                    static_cast<int16_t>(table_id);
            }
        }

        void release_memory() {
            utils::release_vector_memory(rows);
            decltype(overflow)().swap(overflow);
        }
    };
    LookupTableCache lookup_tables_cache;
    /* The goal distances of an abstraction only depend on the costs of its
       relevant operators, so cost functions that agree on them share the
       lookup table. Maps the restricted cost function to the table id (or
       PRUNED_LOOKUP), per abstraction. */
    std::vector<gtl::flat_hash_map<Costs, int, VectorIntMurmurHash>>
    table_by_restricted_costs;
    std::vector<std::vector<int>> relevant_op_ids_by_abstraction;
    Costs restricted_costs_scratch;

    // All DAG nodes with their children; node ids index into the arena.
    NodeArena nodes;

    virtual NodeId create_sscp_order_dag() = 0;

    // A sum node is trivial iff all of its children are lookup nodes.
    bool is_non_trivial_sum_node(NodeId node) const;

    /* Return the lookup node for evaluating the given abstraction under
       the given costs, or NO_NODE if the abstraction is useless for these
       costs. cost_key must be the key registered for costs with
       lookup_costs_or_register(); it is only used if cache_lookup_tables
       is true. */
    NodeId create_lookup_node(
        const Costs &costs, CostKey cost_key, int abstraction_id);

    // Build a CostContext with cost key and operator masks for the costs.
    CostContext make_cost_context(Costs &&costs);

    /* Recompute the mask bits of the given operators from the costs in the
       context, after the costs of these operators changed. */
    void update_cost_context(
        CostContext &context, const std::vector<int> &changed_ops) const;

    /* Reduce the context's costs by the saturated cost function and keep
       hash, key and operator masks in sync. Only visits the operators with
       non-zero saturated cost. */
    void reduce_cost_context(
        CostContext &context, const SaturatedCostFunction &scf);

    std::vector<std::vector<int>> compute_independent_abstractions(
        const std::vector<int> &pending_abstraction_ids,
        const CostContext &context);

    StructuredSCPOrder create_structured_scp_order(NodeId root_node);

    void precompute_operator_properties(
        const std::vector<int> &relevant_abstraction_ids);

    const std::vector<int> &get_lookup_table(NodeId node) const {
        return lookup_tables[nodes[node].abstraction_id]
               [nodes[node].lookup_table_id];
    }

    /* The returned reference lives as long as the generator with
       cache_scf_functions=true; without the cache it is only valid until
       the next call. */
    const SaturatedCostFunction &get_saturated_costs(NodeId node) const;

    /* hash must be the order-independent content hash of costs (sum of
       mix_op_cost over all operators). */
    CostKey lookup_costs_or_register(const Costs &costs, uint64_t hash);

private:
    // Lookup node per abstraction and lookup table.
    std::vector<std::vector<NodeId>> lookup_sscp_node_cache;
    std::vector<std::vector<std::vector<int>>> lookup_tables;
    /* Saturated cost function per abstraction and lookup table. Deques so
       that references stay valid while new entries are added. */
    std::vector<std::deque<SaturatedCostFunction>> scf_cache;
    // Scratch for get_saturated_costs() with cache_scf_functions=false.
    mutable std::unique_ptr<SaturatedCostFunction> scf_scratch;

    std::vector<std::vector<OpMask>> conflicting_ops;
    /* Bit matrix over abstraction pairs: bit id2 in row id1 is set iff the
       pair has any conflicting operator at all. Pairs without a bit can
       never become dependent and are skipped in the independence checks. */
    std::vector<OpMask> statically_conflicting_pairs;
    std::vector<OpMask> relevant_ops_by_abstraction;
    std::vector<OpMask> inf_donating_ops_by_abstraction;
    // Bit set iff the operator is guaranteed nonincreasing (default: set).
    std::vector<OpMask> op_has_nonincreasing_remaining_costs;

    /* Determine for each relevant abstraction the set of operators that
       affects that abstraction. */
    void precompute_relevant_ops(
        const std::vector<bool> &abstraction_is_relevant);

    /* Determine for all operators and relevant abstractions if the operator
       is guaranteed nonincreasing in the abstraction (i.e., if the saturated
       cost of the operator is guaranteed non-negative). */
    void precompute_ops_with_nonincreasing_remaining_cost(
        const std::vector<bool> &abstraction_is_relevant);

    /* Determine the set of operators whose remaining cost can make the
       order of the two abstractions matter. */
    void compute_conflicting_ops(int id1, int id2, OpMask &conflict) const;

    // Precompute the conflicting operators for all pairs of abstractions.
    void precompute_conflicting_ops(
        const std::vector<bool> &abstraction_is_relevant);

    /* Determine for each operator that is potentially conflicting for the
       abstractions id1 and id2 if it is conflicting given the current
       remaining costs. */
    void check_and_add_dependency(
        ccp::DisjointSet &dependency_graph, int id1, int id2,
        const CostContext &context);

    void create_compact_lookup_tables();
};

extern void add_structured_order_generator_options_to_parser(
    plugins::Feature &feature);
extern std::tuple<
    std::shared_ptr<AbstractTask>, Abstractions, StructuredSCPOptions,
    utils::Verbosity>
get_structured_scp_order_generator_arguments_from_options(
    const plugins::Options &opts);
}

#endif
