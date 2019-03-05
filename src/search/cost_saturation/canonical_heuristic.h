#ifndef COST_SATURATION_CANONICAL_HEURISTIC_H
#define COST_SATURATION_CANONICAL_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
using MaxAdditiveSubset = std::vector<int>;
using MaxAdditiveSubsets = std::vector<MaxAdditiveSubset>;

class CanonicalHeuristic : public Heuristic {
    AbstractionFunctions abstraction_functions;
    std::vector<std::vector<int>> h_values_by_abstraction;
    MaxAdditiveSubsets max_additive_subsets;

    int compute_max_h(const std::vector<int> &abstract_state_ids) const;
    int compute_heuristic(const State &state);

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit CanonicalHeuristic(const options::Options &opts);
};
}

#endif
