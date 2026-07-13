#include "structured_scp_order_generator_full.h"

#include "types.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"

#include <algorithm>
#include <bit>
#include <iostream>
#include <numeric>

using namespace std;

namespace cost_saturation {
NodeId StructuredSCPOrderGeneratorFull::create_sscp_order_dag() {
    cout << "Abstractions: " << abstractions.size() << endl;
    vector<int> abstraction_ids(abstractions.size());
    iota(abstraction_ids.begin(), abstraction_ids.end(), 0);

    precompute_operator_properties(abstraction_ids);
    CostContext context = make_cost_context(
        task_properties::get_operator_costs(task_proxy));
    vector<vector<int>> independent_abstractions = use_conflicts
        ? compute_independent_abstractions(abstraction_ids, context)
        : vector<vector<int>>({abstraction_ids});
    assert(!independent_abstractions.empty());
    NodeId root_node = (independent_abstractions.size() == 1)
        ? create_max_node(context, independent_abstractions[0])
        : create_sum_node(context, independent_abstractions);
    // Release the considerable amount of memory used by the hash maps.
    SSCPNodeSet(0, NodeChildrenHash{&nodes}, NodeChildrenEqual{&nodes})
    .swap(sum_sscp_node_post_cache);
    SSCPNodeSet(0, NodeChildrenHash{&nodes}, NodeChildrenEqual{&nodes})
    .swap(max_sscp_node_post_cache);
    decltype(id_set_registry)().swap(id_set_registry);
    decltype(max_sscp_node_pre_cache)().swap(max_sscp_node_pre_cache);

    return root_node;
}

NodeId StructuredSCPOrderGeneratorFull::create_sum_node(
    CostContext &context,
    const vector<vector<int>> &independent_abstractions,
    NodeId scheduled_child) {
    vector<NodeId> children;
    children.reserve(
        independent_abstractions.size() + (scheduled_child != NO_NODE));
    if (scheduled_child != NO_NODE) {
        children.push_back(scheduled_child);
    }

    for (const vector<int> &dependent_abstractions :
         independent_abstractions) {
        NodeId child = create_max_node(context, dependent_abstractions);
        if (child == NO_NODE) {
            continue;
        }
        if (nodes[child].type == NodeType::SUM) {
            // Splice nested sum nodes into this sum node.
            children.insert(
                children.end(), nodes.children_begin(child),
                nodes.children_end(child));
        } else {
            children.push_back(child);
        }
    }
    if (children.empty()) {
        return NO_NODE;
    } else if (children.size() == 1) {
        return children[0];
    }

    sort(children.begin(), children.end());

    if (prune_duplicates) {
        auto it = sum_sscp_node_post_cache.find(children);
        if (it != sum_sscp_node_post_cache.end()) {
            return *it;
        }
    }

    NodeId node = nodes.add_compositional_node(NodeType::SUM, children);
    if (prune_duplicates) {
        sum_sscp_node_post_cache.insert(node);
    }
    return node;
}

namespace {
struct ScheduledChildren {
    vector<int> abstraction_ids;
    vector<NodeId> nodes;
};

void set_op_mask_bit(OpMask &mask, int op_id) {
    mask[op_id / 64] |= uint64_t(1) << (op_id % 64);
}

bool test_op_mask_bit(const OpMask &mask, int op_id) {
    return (mask[op_id / 64] >> (op_id % 64)) & 1;
}

/* Like reduce_cost_context(), but without the guarantee that the saturated
   costs fit into the remaining costs; operators whose remaining cost turns
   negative are recorded in exhausted_ops. */
void reduce_costs_sparse_unguarded(
    Costs &remaining_costs, const SaturatedCostFunction &scf,
    OpMask &exhausted_ops) {
    for (size_t i = 0; i < scf.nonzero_ops.size(); ++i) {
        int op_id = scf.nonzero_ops[i];
        int remaining = remaining_costs[op_id];
        int saturated = scf.nonzero_costs[i];
        assert(remaining == INF || saturated != INF);
        // Left addition: x - y = x for all values y if x is infinite.
        if (remaining != INF) {
            int reduced = (saturated == -INF)
                ? INF
                : static_cast<int>(
                      static_cast<unsigned int>(remaining) -
                      static_cast<unsigned int>(saturated));
            remaining_costs[op_id] = reduced;
            if (reduced < 0) {
                set_op_mask_bit(exhausted_ops, op_id);
            }
        }
    }
}

/* Like reduce_costs_sparse_unguarded(), but additionally recording the
   operators with negative saturated cost. */
void reduce_costs_sparse_unguarded_and_track_negative(
    Costs &remaining_costs, const SaturatedCostFunction &scf,
    OpMask &negative_scf_ops, OpMask &exhausted_ops) {
    for (size_t i = 0; i < scf.nonzero_ops.size(); ++i) {
        int op_id = scf.nonzero_ops[i];
        int remaining = remaining_costs[op_id];
        int saturated = scf.nonzero_costs[i];
        assert(remaining == INF || saturated != INF);
        // Left addition: x - y = x for all values y if x is infinite.
        if (remaining != INF) {
            int reduced = (saturated == -INF)
                ? INF
                : static_cast<int>(
                      static_cast<unsigned int>(remaining) -
                      static_cast<unsigned int>(saturated));
            remaining_costs[op_id] = reduced;
            if (reduced < 0) {
                set_op_mask_bit(exhausted_ops, op_id);
            }
        }
        if (saturated < 0) {
            set_op_mask_bit(negative_scf_ops, op_id);
        }
    }
}
}

NodeId StructuredSCPOrderGeneratorFull::create_max_node(
    CostContext &context, const vector<int> &dependent_abstractions) {
    const Costs &costs = context.costs;
    CostKey cost_key = context.key;
    /* If the input set was the scheduled set of an earlier call with these
       costs (always the case when a max node was split into a sum), the
       cache already knows the result. Probe read-only: only scheduled sets
       are stored, which keeps the cache small. */
    if (prune_duplicates) {
        NodeId cached = find_cached_max_node(cost_key, dependent_abstractions);
        if (cached != UNCACHED_NODE) {
            return cached;
        }
    }
    const bool check_cost_partitioning =
        use_conflicts && options.use_cost_partitioning_check;
    ScheduledChildren scheduled_children;
    Costs overall_remaining_costs(costs);
    /* Track per operator whether some child has negative saturated cost
       and whether the children together want more cost than is available.
       Updating word masks per child is cache-friendlier than checking all
       children per operator below. */
    size_t num_mask_words = (costs.size() + 63) / 64;
    OpMask negative_scf_ops;
    OpMask exhausted_ops;
    if (check_cost_partitioning) {
        exhausted_ops.assign(num_mask_words, 0);
        if (options.use_general_cp) {
            negative_scf_ops.assign(num_mask_words, 0);
        }
    }
    for (int abstraction_id : dependent_abstractions) {
        NodeId scheduled_child =
            create_lookup_node(costs, cost_key, abstraction_id);
        if (scheduled_child != NO_NODE) {
            if (check_cost_partitioning) {
                const SaturatedCostFunction &saturated_cost =
                    get_saturated_costs(scheduled_child);
                /* Use the unguarded reduction to see if the children
                   together want more cost than what is available. */
                if (options.use_general_cp) {
                    reduce_costs_sparse_unguarded_and_track_negative(
                        overall_remaining_costs, saturated_cost,
                        negative_scf_ops, exhausted_ops);
                } else {
                    reduce_costs_sparse_unguarded(
                        overall_remaining_costs, saturated_cost,
                        exhausted_ops);
                }
            }
            scheduled_children.abstraction_ids.push_back(abstraction_id);
            scheduled_children.nodes.push_back(scheduled_child);
        }
    }
    if (scheduled_children.abstraction_ids.empty()) {
        return NO_NODE;
    } else if (scheduled_children.abstraction_ids.size() == 1) {
        return scheduled_children.nodes[0];
    }

    /* Simulate infinite costs for operators whose cost is not exhausted by
       computing all children on the same cost function. If this makes some
       abstractions independent, we can split this max node into a sum. */
    /* With all label options on, the context's live mask is exactly the
       operators with positive finite cost, so the simulation candidates
       follow from three word masks; simulating infinite cost just clears
       their live bits, and the dependency oracle only reads the masks.
       Exhausted bits are only reliable for operators without negative
       saturated costs, which the negative mask excludes anyway. */
    const bool simulate_with_masks = options.use_affecting_labels &&
        options.use_infinite_labels && options.use_non_negative_labels;
    vector<vector<int>> independent_abstractions;
    bool split_computed = false;
    if (check_cost_partitioning && simulate_with_masks) {
        CostContext simulated_context;
        for (size_t w = 0; w < num_mask_words; ++w) {
            uint64_t word = context.live_ops[w] & ~exhausted_ops[w];
            if (options.use_general_cp) {
                word &= ~negative_scf_ops[w];
            }
            if (word) {
                if (simulated_context.live_ops.empty()) {
                    simulated_context.live_ops = context.live_ops;
                    simulated_context.cond_ops = context.cond_ops;
                }
                simulated_context.live_ops[w] &= ~word;
            }
        }
        if (!simulated_context.live_ops.empty()) {
            independent_abstractions = compute_independent_abstractions(
                scheduled_children.abstraction_ids, simulated_context);
            split_computed = true;
        }
    } else if (check_cost_partitioning) {
        vector<int> simulated_ops;
        for (size_t op_id = 0; op_id < costs.size(); ++op_id) {
            if (overall_remaining_costs[op_id] >= 0 && costs[op_id] > 0 &&
                costs[op_id] != INF &&
                (!options.use_general_cp ||
                 !test_op_mask_bit(negative_scf_ops, op_id))) {
                simulated_ops.push_back(op_id);
            }
        }
        if (!simulated_ops.empty()) {
            CostContext simulated_context(context);
            for (int op_id : simulated_ops) {
                simulated_context.costs[op_id] = INF;
            }
            update_cost_context(simulated_context, simulated_ops);
            independent_abstractions = compute_independent_abstractions(
                scheduled_children.abstraction_ids, simulated_context);
            split_computed = true;
        }
    }
    if (!split_computed) {
        independent_abstractions = {scheduled_children.abstraction_ids};
    }
    if (independent_abstractions.size() > 1) {
        return create_sum_node(context, independent_abstractions);
    }

    assert(utils::is_sorted_unique(scheduled_children.abstraction_ids));
    uint64_t scheduled_key = 0;
    if (prune_duplicates) {
        scheduled_key =
            make_call_key(cost_key, scheduled_children.abstraction_ids);
        auto it = max_sscp_node_pre_cache.find(scheduled_key);
        if (it != max_sscp_node_pre_cache.end()) {
            return it->second;
        }
    }

    gtl::flat_hash_set<NodeId> unique_children;
    assert(dependent_abstractions.size() >= 2);
    /* Per-frame save buffer: the recursion below re-enters this function,
       so the buffer must not be shared across frames. */
    vector<int> saved_costs;
    saved_costs.reserve(64);
    for (size_t i = 0; i < scheduled_children.abstraction_ids.size(); ++i) {
        int abstraction_id = scheduled_children.abstraction_ids[i];
        NodeId scheduled_child = scheduled_children.nodes[i];
        assert(scheduled_child != NO_NODE);
        const SaturatedCostFunction &scf =
            get_saturated_costs(scheduled_child);
        /* Reduce the context in place and restore it after handling this
           child; the delta is sparse, a full copy is not. */
        saved_costs.clear();
        for (int op_id : scf.nonzero_ops) {
            saved_costs.push_back(context.costs[op_id]);
        }
        uint64_t saved_hash = context.cost_hash;
        CostKey saved_key = context.key;
        reduce_cost_context(context, scf);
        const vector<int> &base_abstractions = options.use_general_cp
            ? dependent_abstractions
            : scheduled_children.abstraction_ids;
        vector<int> remaining_abstractions(base_abstractions.size() - 1);
        size_t next = 0;
        for (int abstr_id : base_abstractions) {
            if (abstr_id != abstraction_id) {
                remaining_abstractions[next++] = abstr_id;
            }
        }
        assert(next == remaining_abstractions.size());
        vector<vector<int>> independent_remaining_abstractions;
        /* If the reduced costs and remaining set already have a memoized
           max node, the partition is irrelevant: the recursion returns the
           cached node right away. Only memoized sets that did not split
           when they were scheduled can hit, so skipping the (quadratic)
           partition on a hit reproduces the DAG exactly. */
        if (use_conflicts &&
            !(prune_duplicates &&
              find_cached_max_node(context.key, remaining_abstractions) !=
              UNCACHED_NODE)) {
            independent_remaining_abstractions =
                compute_independent_abstractions(
                    remaining_abstractions, context);
        } else {
            independent_remaining_abstractions = {remaining_abstractions};
        }
        NodeId child = create_sum_node(
            context, independent_remaining_abstractions, scheduled_child);
        assert(child != NO_NODE);

        // Restore the context for the next child.
        for (size_t j = 0; j < scf.nonzero_ops.size(); ++j) {
            context.costs[scf.nonzero_ops[j]] = saved_costs[j];
        }
        context.cost_hash = saved_hash;
        context.key = saved_key;
        update_cost_context(context, scf.nonzero_ops);

        if (nodes[child].type == NodeType::MAX) {
            // Splice nested max nodes into this max node.
            unique_children.insert(
                nodes.children_begin(child), nodes.children_end(child));
        } else {
            unique_children.insert(child);
        }
    }

    NodeId max_node;
    if (unique_children.empty()) {
        max_node = NO_NODE;
    } else if (unique_children.size() == 1) {
        max_node = *unique_children.begin();
    } else {
        vector<NodeId> children(
            unique_children.begin(), unique_children.end());
        // Sort children for the dedup key.
        sort(children.begin(), children.end());

        auto it = prune_duplicates
            ? max_sscp_node_post_cache.find(children)
            : max_sscp_node_post_cache.end();
        if (it != max_sscp_node_post_cache.end()) {
            max_node = *it;
        } else {
            max_node = nodes.add_compositional_node(NodeType::MAX, children);
            if (prune_duplicates) {
                max_sscp_node_post_cache.insert(max_node);
            }
        }
    }

    if (prune_duplicates) {
        max_sscp_node_pre_cache[scheduled_key] = max_node;
    }
    return max_node;
}

class StructuredSCPOrderGeneratorFullFeature
    : public plugins::TypedFeature<
          StructuredSCPOrderGenerator, StructuredSCPOrderGeneratorFull> {
public:
    StructuredSCPOrderGeneratorFullFeature()
        : TypedFeature("structured_order_generator_full") {
        document_title("Full structured SCP order generator");
        document_synopsis(
            "Generate a DAG of saturated cost partitioning orders that "
            "represents all orders of the given abstractions, using "
            "duplicate and conflict pruning to keep the DAG small.");
        add_structured_order_generator_options_to_parser(*this);
        add_option<bool>(
            "prune_duplicates", "prune duplicate DAG nodes", "true");
        add_option<bool>(
            "check_conflicts",
            "use the conflict graph to prune the SCP order DAG", "true");

    }

    virtual shared_ptr<StructuredSCPOrderGeneratorFull> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            StructuredSCPOrderGeneratorFull>(
            get_structured_scp_order_generator_arguments_from_options(opts),
            opts.get<bool>("prune_duplicates"),
            opts.get<bool>("check_conflicts"));
    }
};

static plugins::FeaturePlugin<StructuredSCPOrderGeneratorFullFeature> _plugin;
}
