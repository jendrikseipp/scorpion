#ifndef COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_FULL_H
#define COST_SATURATION_STRUCTURED_SCP_ORDER_GENERATOR_FULL_H

#include "structured_scp_order_generator.h"
#include "types.h"

#include <memory>
#include <vector>

namespace cost_saturation {
class StructuredSCPOrderGeneratorFull : public StructuredSCPOrderGenerator {
public:
    StructuredSCPOrderGeneratorFull(
        const std::shared_ptr<AbstractTask> &transform,
        Abstractions abstractions, const StructuredSCPOptions &options,
        utils::Verbosity verbosity, bool prune_duplicates,
        bool use_conflicts)
        : StructuredSCPOrderGenerator(
              transform, move(abstractions), options, verbosity),
          sum_sscp_node_post_cache(
              0, NodeChildrenHash{&nodes}, NodeChildrenEqual{&nodes}),
          max_sscp_node_post_cache(
              0, NodeChildrenHash{&nodes}, NodeChildrenEqual{&nodes}),
          prune_duplicates(prune_duplicates),
          use_conflicts(use_conflicts) {
    }

protected:
    NodeId create_sscp_order_dag() override;

private:
    NodeId create_sum_node(
        const CostContext &context,
        const std::vector<std::vector<int>> &independent_abstractions,
        NodeId scheduled_child = NO_NODE);
    NodeId create_max_node(
        const CostContext &context,
        const std::vector<int> &dependent_abstractions);

    /* Deduplicate compositional nodes by their children ids. Sets of node
       ids with transparent lookup avoid storing a copy of the children
       vector per entry. */
    using SSCPNodeSet = gtl::flat_hash_set<
        NodeId, NodeChildrenHash, NodeChildrenEqual>;
    SSCPNodeSet sum_sscp_node_post_cache;
    SSCPNodeSet max_sscp_node_post_cache;
    using MaxSSCPNodeHashMap =
        gtl::flat_hash_map<NodeKey, NodeId, NodeKeyHash>;
    MaxSSCPNodeHashMap max_sscp_node_pre_cache;

    const bool prune_duplicates;
    const bool use_conflicts;
};
}

#endif
