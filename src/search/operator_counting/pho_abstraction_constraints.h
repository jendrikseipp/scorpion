#ifndef OPERATOR_COUNTING_PHO_ABSTRACTION_CONSTRAINTS_H
#define OPERATOR_COUNTING_PHO_ABSTRACTION_CONSTRAINTS_H

#include "constraint_generator.h"

#include "../cost_saturation/types.h"

#include <memory>

namespace lp {
class LPConstraint;
}

namespace options {
class Options;
}

namespace operator_counting {
class PhOAbstractionConstraints : public ConstraintGenerator {
    const cost_saturation::AbstractionGenerators abstraction_generators;
    const bool saturated;

    cost_saturation::AbstractionFunctions abstraction_functions;
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<int> constraint_ids_by_abstraction;
    std::vector<bool> useless_operators;
public:
    explicit PhOAbstractionConstraints(const options::Options &opts);

    virtual void initialize_constraints(
        const std::shared_ptr<AbstractTask> &task,
        lp::LinearProgram &lp) override;
    virtual bool update_constraints(
        const State &state, lp::LPSolver &lp_solver) override;
};
}

#endif
