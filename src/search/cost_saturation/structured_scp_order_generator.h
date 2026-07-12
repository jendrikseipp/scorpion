#ifndef COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_H
#define COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_H

#include "cost_function_registry.h"
#include "sscp_dag.h"
#include "types.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../algorithms/connected_components.h"
#include "../utils/collections.h"
#include "../utils/logging.h"

#include "gtl/phmap.hpp"

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <vector>

namespace plugins {
class Feature;
class Options;
}

namespace cost_saturation {
// Bit mask over the task's operators, one bit per operator.
using OpMask = std::vector<uint64_t>;

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

struct SaturatedCostFunction {
    Costs costs;
    /* Operators with non-zero saturated cost. Saturated cost functions are
       usually sparse, so loops over them only need to visit these
       operators. */
    std::vector<int> nonzero_ops;

    explicit SaturatedCostFunction(Costs &&costs)
        : costs(std::move(costs)) {
        for (size_t op_id = 0; op_id < this->costs.size(); ++op_id) {
            if (this->costs[op_id] != 0) {
                nonzero_ops.push_back(op_id);
            }
        }
    }
};

/*
  A cost function together with a classification of its operators for the
  dependency checks: live operators always create a dependency between
  abstractions they affect; conditional operators (remaining cost 0) only
  create a dependency between abstractions that do not both guarantee
  nonincreasing remaining costs. Everything is maintained incrementally as
  costs are reduced along the DAG construction.
*/
struct CostContext {
    Costs costs;
    /* Key of the cost function in the generator's cost function registry
       and an order-independent content hash that is updated incrementally
       as costs are reduced. Both are only valid when the context was
       created or reduced by the generator; temporarily simulated costs
       leave them stale. */
    CostKey key;
    uint64_t cost_hash;
    OpMask live_ops;
    OpMask cond_ops;
};

// Sentinel table id caching that the lookup node was pruned for the costs.
constexpr int PRUNED_LOOKUP = -1;
// Sentinel for lookup nodes that have not been computed yet.
constexpr int UNKNOWN_LOOKUP = -2;

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
    CostFunctionRegistry cost_functions;
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
                row.assign(
                    num_abstractions,
                    static_cast<int16_t>(UNKNOWN_LOOKUP));
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
       costs. cost_key must be the registered key of costs; it is only used
       if cache_lookup_tables is true. */
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

private:
    // Lookup node per abstraction and lookup table.
    std::vector<std::vector<NodeId>> lookup_nodes;
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
