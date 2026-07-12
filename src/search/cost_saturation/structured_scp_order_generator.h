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

/*
  Node in the DAG that represents a structured saturated cost partitioning:
  leaves look up goal distances in tables, inner nodes maximize or sum over
  the values of their children.
*/
struct SSCPNode {
    /* This is a unique positive value for max and sum nodes, and a unique
       negative one for lookup nodes. */
    int index;

    // The level corresponds to the depth of the DAG rooted at this node.
    int level;

    std::vector<std::shared_ptr<SSCPNode>> children;

    bool complete;

    // Used only for lookup nodes.
    SSCPNode()
        : level(0), complete(true) {
        ++num_lookup_nodes;
        index = -num_lookup_nodes;
    }

    // Used only for compositional (max and sum) nodes.
    explicit SSCPNode(std::vector<std::shared_ptr<SSCPNode>> &&children)
        : index(num_compositional_nodes),
          level(0),
          children(move(children)),
          complete(false) {
        ++num_compositional_nodes;
    }

    virtual ~SSCPNode() = default;

    virtual void update() {}

    static int num_compositional_nodes;
    static int num_lookup_nodes;
};

struct CompositionalSSCPNode : public SSCPNode {
    explicit CompositionalSSCPNode(
        std::vector<std::shared_ptr<SSCPNode>> &&children)
        : SSCPNode(move(children)) {
    }

    void update() override {
        level = 0;
        complete = !children.empty();
        for (const std::shared_ptr<SSCPNode> &child : children) {
            if (child) {
                level = std::max(level, child->level);
                complete = complete && child->complete;
            } else {
                complete = false;
            }
        }
        ++level;
    }
};

struct MaxSSCPNode : public CompositionalSSCPNode {
    explicit MaxSSCPNode(std::vector<std::shared_ptr<SSCPNode>> &&children)
        : CompositionalSSCPNode(move(children)) {
        ++num_max_nodes;
    }

    static int num_max_nodes;
};

struct SumSSCPNode : public CompositionalSSCPNode {
    explicit SumSSCPNode(std::vector<std::shared_ptr<SSCPNode>> &&children)
        : CompositionalSSCPNode(move(children)) {
        ++num_sum_nodes;
        if (is_non_trivial()) {
            ++num_nontrivial_sum_nodes;
        }
    }

    // A SumSSCPNode is trivial iff all of its children are LookupSSCPNodes.
    bool is_non_trivial() const;

    static int num_sum_nodes;
    static int num_nontrivial_sum_nodes;
};

struct LookupSSCPNode : public SSCPNode {
    int abstraction_id;
    int lookup_table_id;

    LookupSSCPNode(int abstraction_id, int lookup_table_id)
        : SSCPNode(),
          abstraction_id(abstraction_id),
          lookup_table_id(lookup_table_id) {
    }

    void update() override {
        assert(level == 0);
        assert(complete);
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

    void reserve(int num_instructions, int num_ids) {
        types.reserve(num_instructions);
        id_offsets.reserve(num_instructions + 1);
        id_offsets.push_back(0);
        ids.reserve(num_ids);
    }

    void append(
        InstructionType type, const std::vector<
            std::shared_ptr<SSCPNode>> &children) {
        types.push_back(type);
        for (const std::shared_ptr<SSCPNode> &child : children) {
            ids.push_back(child->index);
        }
        id_offsets.push_back(ids.size());
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
  the indices of their children, so the caches can store the nodes
  themselves instead of a copy of the index vector per entry.
*/
struct NodeChildrenHash {
    using is_transparent = void;

    size_t operator()(const std::vector<int> &child_indices) const {
        return VectorIntMurmurHash()(child_indices);
    }

    size_t operator()(const std::shared_ptr<SSCPNode> &node) const {
        // Only called on rehashes; queries hash the index vector directly.
        std::vector<int> child_indices;
        child_indices.reserve(node->children.size());
        for (const std::shared_ptr<SSCPNode> &child : node->children) {
            child_indices.push_back(child->index);
        }
        return VectorIntMurmurHash()(child_indices);
    }
};

struct NodeChildrenEqual {
    using is_transparent = void;

    bool operator()(
        const std::shared_ptr<SSCPNode> &node,
        const std::vector<int> &child_indices) const {
        if (node->children.size() != child_indices.size()) {
            return false;
        }
        for (size_t i = 0; i < child_indices.size(); ++i) {
            if (node->children[i]->index != child_indices[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator()(
        const std::vector<int> &child_indices,
        const std::shared_ptr<SSCPNode> &node) const {
        return (*this)(node, child_indices);
    }

    bool operator()(
        const std::shared_ptr<SSCPNode> &node1,
        const std::shared_ptr<SSCPNode> &node2) const {
        if (node1->children.size() != node2->children.size()) {
            return false;
        }
        for (size_t i = 0; i < node1->children.size(); ++i) {
            if (node1->children[i]->index != node2->children[i]->index) {
                return false;
            }
        }
        return true;
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

    virtual std::shared_ptr<SSCPNode> create_sscp_order_dag() = 0;

    /* cost_key must be the key registered for costs with
       lookup_costs_or_register(); it is only used if cache_lookup_tables
       is true. */
    std::shared_ptr<LookupSSCPNode> create_lookup_node(
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

    StructuredSCPOrder create_structured_scp_order(
        std::shared_ptr<SSCPNode> &root_node);

    void precompute_operator_properties(
        const std::vector<int> &relevant_abstraction_ids);

    const std::vector<int> &get_lookup_table(
        const std::shared_ptr<LookupSSCPNode> &node) const {
        return lookup_tables[node->abstraction_id][node->lookup_table_id];
    }

    /* The returned reference lives as long as the generator with
       cache_scf_functions=true; without the cache it is only valid until
       the next call. */
    const SaturatedCostFunction &get_saturated_costs(
        const std::shared_ptr<LookupSSCPNode> &node) const;

    CostKey lookup_costs_or_register(const Costs &costs);

private:
    std::vector<std::vector<std::shared_ptr<LookupSSCPNode>>>
    lookup_sscp_node_cache;
    std::vector<std::vector<std::vector<int>>> lookup_tables;
    /* Saturated cost function per lookup node. Deque so that references
       stay valid while new entries are added. */
    std::deque<SaturatedCostFunction> scf_cache;
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
