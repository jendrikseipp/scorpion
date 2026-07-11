#ifndef COST_SATURATION_STRUCTURED_SCP_ORDER_HEURISTIC_H
#define COST_SATURATION_STRUCTURED_SCP_ORDER_HEURISTIC_H

#include "structured_scp_order_generator.h"
#include "types.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace cost_saturation {
class StructuredSCPOrderHeuristic : public Heuristic {
    AbstractionFunctions abs_functions;
    std::vector<UnsolvabilityInfo> unsolvability_infos;
    std::vector<Instruction> instructions;
    std::vector<std::vector<std::vector<int>>> lookup_tables;
    std::vector<int> values;

public:
    StructuredSCPOrderHeuristic(
        const std::shared_ptr<AbstractTask> &transform, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity,
        AbstractionFunctions &&abs_functions,
        std::vector<UnsolvabilityInfo> &&unsolvability_infos,
        std::vector<Instruction> &&instructions,
        std::vector<std::vector<std::vector<int>>> &&lookup_tables);

    virtual int compute_heuristic(const State &ancestor_state) override;
};
}

#endif
