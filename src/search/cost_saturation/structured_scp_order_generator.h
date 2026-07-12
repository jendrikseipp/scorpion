#ifndef COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_H
#define COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_H

#include "types.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../algorithms/connected_components.h"
#include "../utils/logging.h"

#include "gtl/phmap.hpp"

#include <cstdint>
#include <deque>
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
  the values of their children. Nodes live in the arena of their generator
  and refer to their children by node id.
*/
struct SSCPNode {
    NodeType type;

    // The level corresponds to the depth of the DAG rooted at this node.
    int level;

    // Children of max and sum nodes (empty for lookup nodes).
    std::vector<NodeId> children;

    // Lookup table position (only used for lookup nodes).
    int abstraction_id;
    int lookup_table_id;
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

    const std::vector<SSCPNode> *nodes;

    size_t operator()(const std::vector<NodeId> &children) const {
        return VectorIntMurmurHash()(children);
    }

    size_t operator()(NodeId node) const {
        return VectorIntMurmurHash()((*nodes)[node].children);
    }
};

struct NodeChildrenEqual {
    using is_transparent = void;

    const std::vector<SSCPNode> *nodes;

    bool operator()(NodeId node, const std::vector<NodeId> &children) const {
        return (*nodes)[node].children == children;
    }

    bool operator()(const std::vector<NodeId> &children, NodeId node) const {
        return (*nodes)[node].children == children;
    }

    bool operator()(NodeId node1, NodeId node2) const {
        return (*nodes)[node1].children == (*nodes)[node2].children;
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
    std::vector<std::vector<uint8_t>> packed_costs_by_key;
    /* lookup_tables_cache[cost_key][abstraction_id] is the lookup table id
       for evaluating the abstraction under the cost function with this key
       (UNKNOWN_LOOKUP if not computed yet, PRUNED_LOOKUP if pruned). Rows
       are created on the first lookup with a cost key. */
    std::vector<std::vector<int>> lookup_tables_cache;

    // Arena holding all DAG nodes; node ids are indices into this vector.
    std::vector<SSCPNode> nodes;

    virtual NodeId create_sscp_order_dag() = 0;

    NodeId add_compositional_node(
        NodeType type, std::vector<NodeId> &&children);

    // A sum node is trivial iff all of its children are lookup nodes.
    bool is_non_trivial_sum_node(NodeId node) const;

    /* Return the lookup node for evaluating the given abstraction under
       the given costs, or NO_NODE if the abstraction is useless for these
       costs. cost_key must be the key registered for costs with
       lookup_costs_or_register(); it is only used if cache_lookup_tables
       is true. */
    NodeId create_lookup_node(
        const Costs &costs, CostKey cost_key, int abstraction_id);

    // Build a CostContext with operator masks for the given costs.
    CostContext make_cost_context(Costs &&costs) const;

    /* Recompute the mask bits of the given operators from the costs in the
       context, after the costs of these operators changed. */
    void update_cost_context(
        CostContext &context, const std::vector<int> &changed_ops) const;

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

    CostKey lookup_costs_or_register(const Costs &costs);

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
    std::vector<OpMask> relevant_ops_by_abstraction;
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

    /* Determine for each pair of relevant abstractions the set of
       conflicting operators (i.e., the operators that affect both
       abstractions). */
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
