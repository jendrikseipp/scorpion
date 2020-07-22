#ifndef OPERATOR_COUNTING_PHO_ABSTRACTION_CONSTRAINTS_H
#define OPERATOR_COUNTING_PHO_ABSTRACTION_CONSTRAINTS_H

#include "constraint_generator.h"

#include "../cost_saturation/types.h"

#include <memory>

namespace options {
class Options;
}

namespace cost_saturation {
class AbstractionGenerator;
}

namespace operator_counting {
class PhOAbstractionConstraints : public ConstraintGenerator {
    const std::vector<std::shared_ptr<cost_saturation::AbstractionGenerator>> abstraction_generators;
    const bool saturated;
    const bool counting;
    const bool consider_finite_negative_saturated_costs;
    const bool forbid_useless_operators;
    const bool ignore_goal_out_operators;

    cost_saturation::AbstractionFunctions abstraction_functions;
    std::vector<std::vector<int>> h_values_by_abstraction;
    int constraint_offset;
public:
    explicit PhOAbstractionConstraints(const options::Options &opts);

    virtual void initialize_constraints(
        const std::shared_ptr<AbstractTask> &task,
        std::vector<lp::LPVariable> &variables,
        std::vector<lp::LPConstraint> &constraints,
        double infinity) override;
    virtual bool update_constraints(
        const State &state, lp::LPSolver &lp_solver) override;
};
}

#endif
