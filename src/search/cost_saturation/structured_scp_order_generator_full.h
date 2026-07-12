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
        Abstractions abstractions, bool use_unsolvability_infos,
        bool use_general_cp, utils::Verbosity verbosity,
        bool prune_duplicates, bool use_conflicts, bool cache_lookup_tables)
        : StructuredSCPOrderGenerator(
              transform, move(abstractions), use_unsolvability_infos,
              use_general_cp, cache_lookup_tables, verbosity),
          prune_duplicates(prune_duplicates),
          use_conflicts(use_conflicts) {
    }

protected:
    std::shared_ptr<SSCPNode> create_sscp_order_dag() override;

private:
    std::shared_ptr<SSCPNode> create_sum_node(
        const Costs &costs,
        const std::vector<std::vector<int>> &independent_abstractions,
        std::shared_ptr<LookupSSCPNode> &&scheduled_child = nullptr);
    std::shared_ptr<SSCPNode> create_max_node(
        const Costs &costs, const std::vector<int> &dependent_abstractions);

    using SSCPNodeHashMap = gtl::flat_hash_map<
        std::vector<int>, std::shared_ptr<SSCPNode>, VectorIntMurmurHash>;
    SSCPNodeHashMap sum_sscp_node_post_cache;
    SSCPNodeHashMap max_sscp_node_post_cache;
    using MaxSSCPNodeHashMap = gtl::flat_hash_map<
        NodeKey, std::shared_ptr<SSCPNode>, NodeKeyHash>;
    MaxSSCPNodeHashMap max_sscp_node_pre_cache;

    const bool prune_duplicates;
    const bool use_conflicts;
};
}

#endif
