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

    cost_saturation::Abstractions abstractions;
    std::vector<std::vector<int>> h_values_by_abstraction;
    std::vector<int> operator_costs;
    int constraint_offset;
public:
    explicit PhOAbstractionConstraints(const options::Options &opts);

    virtual void initialize_constraints(
        const std::shared_ptr<AbstractTask> task,
        std::vector<lp::LPConstraint> &constraints,
        double infinity) override;
    virtual bool update_constraints(
        const State &state, lp::LPSolver &lp_solver) override;
};
}

#endif
