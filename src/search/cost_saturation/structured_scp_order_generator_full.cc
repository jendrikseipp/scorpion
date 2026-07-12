#include "structured_scp_order_generator_full.h"

#include "types.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"

#include <algorithm>
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
    const CostContext &context,
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

/* Like reduce_cost_context(), but without the guarantee that the saturated
   costs fit into the remaining costs, and additionally recording the
   operators with negative saturated cost. */
void reduce_costs_sparse_unguarded_and_track_negative(
    Costs &remaining_costs, const SaturatedCostFunction &scf,
    vector<uint8_t> &op_has_negative_scf) {
    assert(remaining_costs.size() == scf.costs.size());
    assert(op_has_negative_scf.size() == scf.costs.size());
    for (int op_id : scf.nonzero_ops) {
        int remaining = remaining_costs[op_id];
        int saturated = scf.costs[op_id];
        assert(remaining == INF || saturated != INF);
        // Left addition: x - y = x for all values y if x is infinite.
        if (remaining != INF) {
            remaining_costs[op_id] =
                (saturated == -INF)
                ? INF
                : static_cast<int>(
                      static_cast<unsigned int>(remaining) -
                      static_cast<unsigned int>(saturated));
        }
        op_has_negative_scf[op_id] |= (saturated < 0);
    }
}
}

NodeId StructuredSCPOrderGeneratorFull::create_max_node(
    const CostContext &context, const vector<int> &dependent_abstractions) {
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
    /* Track for each operator whether some child has negative saturated
       cost. Updating flags per child is cache-friendlier than checking all
       children per operator below. */
    vector<uint8_t> op_has_negative_scf;
    if (check_cost_partitioning && options.use_general_cp) {
        op_has_negative_scf.assign(costs.size(), false);
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
                        op_has_negative_scf);
                } else {
                    reduce_costs_unguarded(
                        overall_remaining_costs, saturated_cost.costs);
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
    vector<int> simulated_ops;
    if (check_cost_partitioning) {
        for (size_t op_id = 0; op_id < costs.size(); ++op_id) {
            if (overall_remaining_costs[op_id] >= 0 && costs[op_id] > 0 &&
                costs[op_id] != INF &&
                (!options.use_general_cp || !op_has_negative_scf[op_id])) {
                simulated_ops.push_back(op_id);
            }
        }
    }
    vector<vector<int>> independent_abstractions;
    if (!simulated_ops.empty()) {
        CostContext simulated_context(context);
        for (int op_id : simulated_ops) {
            simulated_context.costs[op_id] = INF;
        }
        update_cost_context(simulated_context, simulated_ops);
        independent_abstractions = compute_independent_abstractions(
            scheduled_children.abstraction_ids, simulated_context);
    } else {
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
    for (size_t i = 0; i < scheduled_children.abstraction_ids.size(); ++i) {
        int abstraction_id = scheduled_children.abstraction_ids[i];
        NodeId scheduled_child = scheduled_children.nodes[i];
        assert(scheduled_child != NO_NODE);
        const SaturatedCostFunction &scf =
            get_saturated_costs(scheduled_child);
        CostContext remaining_context(context);
        reduce_cost_context(remaining_context, scf);
        vector<int> remaining_abstractions;
        if (options.use_general_cp) {
            remaining_abstractions.reserve(dependent_abstractions.size() - 1);
            copy_if(dependent_abstractions.begin(),
                    dependent_abstractions.end(),
                    back_inserter(remaining_abstractions),
                    [&](int abstr_id) {return abstr_id != abstraction_id;});
        } else {
            remaining_abstractions.reserve(
                scheduled_children.abstraction_ids.size() - 1);
            copy_if(scheduled_children.abstraction_ids.begin(),
                    scheduled_children.abstraction_ids.end(),
                    back_inserter(remaining_abstractions),
                    [&](int abstr_id) {return abstr_id != abstraction_id;});
        }
        vector<vector<int>> independent_remaining_abstractions;
        if (use_conflicts) {
            independent_remaining_abstractions =
                compute_independent_abstractions(
                    remaining_abstractions, remaining_context);
        } else {
            independent_remaining_abstractions = {remaining_abstractions};
        }
        NodeId child = create_sum_node(
            remaining_context, independent_remaining_abstractions,
            scheduled_child);
        assert(child != NO_NODE);

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
