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
    int compute_max_h(const std::vector<int> &local_state_ids) const;
    int compute_heuristic(const State &state);

protected:
    Abstractions abstractions;
    std::vector<std::vector<int>> h_values_by_abstraction;
    MaxAdditiveSubsets max_additive_subsets;
    const bool debug;

    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit CanonicalHeuristic(const options::Options &opts);
    virtual ~CanonicalHeuristic() override;

    virtual void print_statistics() const override;
};
}

#endif
