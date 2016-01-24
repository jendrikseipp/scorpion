#ifndef CEGAR_OCP_CONSTRAINTS_H
#define CEGAR_OCP_CONSTRAINTS_H

#include "../operator_counting/constraint_generator.h"

#include <memory>

namespace cegar {
class Abstraction;

class OCPConstraints : public operator_counting::ConstraintGenerator {

public:
    explicit OCPConstraints(const Abstraction &abstraction);
    ~OCPConstraints() = default;

    virtual void initialize_constraints(
        const std::shared_ptr<AbstractTask> task,
        std::vector<lp::LPConstraint> &constraints,
        double infinity) override;

    virtual bool update_constraints(
        const State &state, lp::LPSolver &lp_solver) override;
};
}

#endif
