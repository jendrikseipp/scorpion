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
shared_ptr<SSCPNode> StructuredSCPOrderGeneratorFull::create_sscp_order_dag() {
    cout << "Abstractions: " << abstractions.size() << endl;
    vector<int> abstraction_ids(abstractions.size());
    iota(abstraction_ids.begin(), abstraction_ids.end(), 0);

    precompute_operator_properties(abstraction_ids);
    Costs costs = task_properties::get_operator_costs(task_proxy);
    vector<vector<int>> independent_abstractions = use_conflicts
        ? compute_independent_abstractions(abstraction_ids, costs)
        : vector<vector<int>>({abstraction_ids});
    assert(!independent_abstractions.empty());
    shared_ptr<SSCPNode> root_node = (independent_abstractions.size() == 1)
        ? create_max_node(costs, independent_abstractions[0])
        : create_sum_node(costs, independent_abstractions);
    // Release the considerable amount of memory used by the hash maps.
    SSCPNodeHashMap().swap(sum_sscp_node_post_cache);
    SSCPNodeHashMap().swap(max_sscp_node_post_cache);
    MaxSSCPNodeHashMap().swap(max_sscp_node_pre_cache);

    return root_node;
}

shared_ptr<SSCPNode> StructuredSCPOrderGeneratorFull::create_sum_node(
    const Costs &costs,
    const vector<vector<int>> &independent_abstractions,
    shared_ptr<LookupSSCPNode> &&scheduled_child) {
    vector<shared_ptr<SSCPNode>> children;
    children.reserve(
        independent_abstractions.size() + (scheduled_child ? 1 : 0));
    if (scheduled_child) {
        children.push_back(move(scheduled_child));
    }

    for (const vector<int> &dependent_abstractions :
         independent_abstractions) {
        shared_ptr<SSCPNode> child =
            create_max_node(costs, dependent_abstractions);
        if (dynamic_pointer_cast<SumSSCPNode>(child)) {
            // Splice nested sum nodes into this sum node.
            for (const shared_ptr<SSCPNode> &grand_child : child->children) {
                assert(grand_child);
                children.push_back(grand_child);
            }
        } else if (child) {
            children.push_back(move(child));
        }
    }
    if (children.empty()) {
        return nullptr;
    } else if (children.size() == 1) {
        return children[0];
    }

    sort(children.begin(), children.end(),
         [](const shared_ptr<SSCPNode> &lhs, const shared_ptr<SSCPNode> &rhs) {
             assert(lhs->index != rhs->index);
             return lhs->index < rhs->index;
         });

    vector<int> hash_key;
    if (prune_duplicates) {
        hash_key.reserve(children.size());
        for (const shared_ptr<SSCPNode> &child : children) {
            hash_key.push_back(child->index);
        }
        auto it = sum_sscp_node_post_cache.find(hash_key);
        if (it != sum_sscp_node_post_cache.end()) {
            return it->second;
        }
    }

    shared_ptr<SumSSCPNode> node = make_shared<SumSSCPNode>(move(children));
    node->update();
    if (prune_duplicates) {
        sum_sscp_node_post_cache[move(hash_key)] = node;
    }
    return node;
}

namespace {
struct ScheduledChildren {
    vector<int> abstraction_ids;
    vector<shared_ptr<LookupSSCPNode>> nodes;
    vector<Costs> saturated_costs;
};
}

shared_ptr<SSCPNode> StructuredSCPOrderGeneratorFull::create_max_node(
    const Costs &costs, const vector<int> &dependent_abstractions) {
    ScheduledChildren scheduled_children;
    Costs overall_remaining_costs(costs);
    for (int abstraction_id : dependent_abstractions) {
        shared_ptr<LookupSSCPNode> scheduled_child =
            create_lookup_node(costs, abstraction_id);
        if (scheduled_child) {
            Costs saturated_cost = get_saturated_costs(scheduled_child);
            if (use_conflicts && g_hacked_use_cost_partitioning_check) {
                /* Use the unguarded version to see if the children together
                   want more cost than what is available. */
                reduce_costs_unguarded(
                    overall_remaining_costs, saturated_cost);
            }
            scheduled_children.abstraction_ids.push_back(abstraction_id);
            scheduled_children.nodes.push_back(move(scheduled_child));
            scheduled_children.saturated_costs.push_back(
                move(saturated_cost));
        }
    }
    if (scheduled_children.abstraction_ids.empty()) {
        return nullptr;
    } else if (scheduled_children.abstraction_ids.size() == 1) {
        return scheduled_children.nodes[0];
    }

    /* Simulate infinite costs for operators whose cost is not exhausted by
       computing all children on the same cost function. If this makes some
       abstractions independent, we can split this max node into a sum. */
    Costs simulated_costs(costs);
    if (use_conflicts && g_hacked_use_cost_partitioning_check) {
        for (size_t op_id = 0; op_id < costs.size(); ++op_id) {
            if (overall_remaining_costs[op_id] >= 0 && costs[op_id] > 0) {
                if (!use_general_cp) {
                    simulated_costs[op_id] = INF;
                } else {
                    bool independent = all_of(
                        scheduled_children.saturated_costs.begin(),
                        scheduled_children.saturated_costs.end(),
                        [&op_id](const vector<int> &saturated_cost) {
                            return saturated_cost[op_id] >= 0;
                        });
                    if (independent) {
                        simulated_costs[op_id] = INF;
                    }
                }
            }
        }
    }
    vector<vector<int>> independent_abstractions;
    if (use_conflicts && g_hacked_use_cost_partitioning_check &&
        simulated_costs != costs) {
        independent_abstractions = compute_independent_abstractions(
            scheduled_children.abstraction_ids, simulated_costs);
    } else {
        independent_abstractions = {scheduled_children.abstraction_ids};
    }
    if (independent_abstractions.size() > 1) {
        return create_sum_node(costs, independent_abstractions);
    }

    assert(utils::is_sorted_unique(scheduled_children.abstraction_ids));
    NodeKey pre_hash_key;
    if (prune_duplicates) {
        CostKey cost_key = lookup_costs_or_register(costs);
        pre_hash_key = NodeKey(cost_key, scheduled_children.abstraction_ids);
        auto it = max_sscp_node_pre_cache.find(pre_hash_key);
        if (it != max_sscp_node_pre_cache.end()) {
            return it->second;
        }
    }

    gtl::flat_hash_set<shared_ptr<SSCPNode>> unique_children;
    assert(dependent_abstractions.size() >= 2);
    for (size_t i = 0; i < scheduled_children.abstraction_ids.size(); ++i) {
        int abstraction_id = scheduled_children.abstraction_ids[i];
        shared_ptr<LookupSSCPNode> &scheduled_child =
            scheduled_children.nodes[i];
        Costs &saturated_costs = scheduled_children.saturated_costs[i];
        assert(scheduled_child);
        Costs remaining_costs(costs);
        reduce_costs(remaining_costs, saturated_costs);
        vector<int> remaining_abstractions;
        if (use_general_cp) {
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
                    remaining_abstractions, remaining_costs);
        } else {
            independent_remaining_abstractions = {remaining_abstractions};
        }
        shared_ptr<SSCPNode> child = create_sum_node(
            remaining_costs, independent_remaining_abstractions,
            move(scheduled_child));
        assert(child);

        if (dynamic_pointer_cast<MaxSSCPNode>(child)) {
            // Splice nested max nodes into this max node.
            for (const shared_ptr<SSCPNode> &grand_child : child->children) {
                assert(grand_child);
                unique_children.insert(grand_child);
            }
        } else {
            unique_children.insert(move(child));
        }
    }

    shared_ptr<SSCPNode> max_node;
    if (unique_children.empty()) {
        max_node = nullptr;
    } else if (unique_children.size() == 1) {
        max_node = *unique_children.begin();
    } else {
        vector<shared_ptr<SSCPNode>> children(
            unique_children.begin(), unique_children.end());
        // Sort children for the hash key.
        sort(children.begin(), children.end(),
             [](const shared_ptr<SSCPNode> &lhs,
                const shared_ptr<SSCPNode> &rhs) {
                 return lhs->index < rhs->index;
             });

        vector<int> post_hash_key;
        post_hash_key.reserve(children.size());
        for (const shared_ptr<SSCPNode> &child : children) {
            post_hash_key.push_back(child->index);
        }
        auto it = prune_duplicates
            ? max_sscp_node_post_cache.find(post_hash_key)
            : max_sscp_node_post_cache.end();
        if (it != max_sscp_node_post_cache.end()) {
            max_node = it->second;
        } else {
            max_node = make_shared<MaxSSCPNode>(move(children));
            max_node->update();
            if (prune_duplicates) {
                max_sscp_node_post_cache[move(post_hash_key)] = max_node;
            }
        }
    }

    if (prune_duplicates) {
        max_sscp_node_pre_cache[move(pre_hash_key)] = max_node;
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
        add_option<bool>(
            "cache_lookup_tables", "cache lookup tables", "false");
    }

    virtual shared_ptr<StructuredSCPOrderGeneratorFull> create_component(
        const plugins::Options &opts) const override {
        g_hacked_use_affecting_labels = opts.get<bool>("use_affecting_labels");
        g_hacked_use_non_negative_labels =
            opts.get<bool>("use_non_negative_labels");
        g_hacked_use_infinite_labels = opts.get<bool>("use_infinite_labels");
        g_hacked_use_cost_partitioning_check =
            opts.get<bool>("use_cost_partitioning_check");
        g_hacked_cache_scf_functions = opts.get<bool>("cache_scf_functions");
        if (g_hacked_use_cost_partitioning_check &&
            !g_hacked_use_infinite_labels) {
            ABORT("use_cost_partitioning_check=true requires "
                  "use_infinite_labels=true");
        }
        return plugins::make_shared_from_arg_tuples<
            StructuredSCPOrderGeneratorFull>(
            get_structured_scp_order_generator_arguments_from_options(opts),
            opts.get<bool>("prune_duplicates"),
            opts.get<bool>("check_conflicts"),
            opts.get<bool>("cache_lookup_tables"));
    }
};

static plugins::FeaturePlugin<StructuredSCPOrderGeneratorFullFeature> _plugin;
}
