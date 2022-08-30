#ifndef COST_SATURATION_MAX_HEURISTIC_H
#define COST_SATURATION_MAX_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

namespace cost_saturation {
class MaxHeuristic : public Heuristic {
    AbstractionFunctions abstraction_functions;
    std::vector<std::vector<int>> h_values_by_abstraction;

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    MaxHeuristic(const options::Options &opts, const Abstractions &abstractions);
};
}

#endif
