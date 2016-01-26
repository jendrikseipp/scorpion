#ifndef CEGAR_OCP_CONSTRAINTS_H
#define CEGAR_OCP_CONSTRAINTS_H

#include "../operator_counting/constraint_generator.h"

class TaskProxy;

namespace cegar {
class Abstraction;

class OCPConstraints : public operator_counting::ConstraintGenerator {
    std::vector<lp::LPConstraint> ocp_constraints;
    int num_transitions;
    int num_goals;
    std::size_t init_offset;
    std::size_t goals_offset;
    std::size_t transitions_offset;

public:
    explicit OCPConstraints(
        const TaskProxy &subtask_proxy, const Abstraction &abstraction);
    ~OCPConstraints() = default;

    virtual void initialize_variables(
        const std::shared_ptr<AbstractTask> task,
        std::vector<lp::LPVariable> &variables,
        double infinity) override;


    virtual void initialize_constraints(
        const std::shared_ptr<AbstractTask> task,
        std::vector<lp::LPConstraint> &constraints,
        double infinity) override;

    virtual bool update_constraints(
            const State &state, lp::LPSolver &lp_solver) override;
};
}

#endif
