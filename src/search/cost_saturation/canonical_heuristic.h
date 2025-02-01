#ifndef COST_SATURATION_CANONICAL_HEURISTIC_H
#define COST_SATURATION_CANONICAL_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <vector>

namespace cost_saturation {
using MaxAdditiveSubset = std::vector<int>;
using MaxAdditiveSubsets = std::vector<MaxAdditiveSubset>;

class CanonicalHeuristic : public Heuristic {
    AbstractionFunctions abstraction_functions;
    std::vector<std::vector<int>> h_values_by_abstraction;
    MaxAdditiveSubsets max_additive_subsets;

    int compute_max_over_sums(const std::vector<int> &h_values_for_state) const;

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    CanonicalHeuristic(
        const std::vector<std::shared_ptr<AbstractionGenerator>> &abstraction_generators,
        const std::shared_ptr<AbstractTask> &transform, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);
};
}

#endif
