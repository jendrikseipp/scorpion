#ifndef COST_SATURATION_MAX_HEURISTIC_H
#define COST_SATURATION_MAX_HEURISTIC_H

#include "abstraction_generator.h"
#include "types.h"

#include "../heuristic.h"

namespace cost_saturation {
class MaxHeuristic : public Heuristic {
    AbstractionFunctions abstraction_functions;
    std::vector<std::vector<int>> h_values_by_abstraction;

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    MaxHeuristic(
        const std::shared_ptr<AbstractTask> &task,
        const std::vector<std::shared_ptr<AbstractionGenerator>>
            &abstraction_generators,
        bool cache_estimates, const std::string &description,
        utils::Verbosity verbosity);
};
}

#endif
