#ifndef CEGAR_OCP_CONSTRAINTS_H
#define CEGAR_OCP_CONSTRAINTS_H

#include "../operator_counting/constraint_generator.h"

#include <unordered_map>
#include <unordered_set>

class TaskProxy;

namespace cegar {
class AbstractState;
class Abstraction;

class OCPConstraints : public operator_counting::ConstraintGenerator {
    std::vector<lp::LPConstraint> ocp_constraints;
    int num_transitions;
    int num_goals;
    std::size_t init_offset;
    std::size_t goals_offset;
    std::size_t transitions_offset;
    std::unordered_map<int, std::vector<int>> operator_to_transitions;
    std::unordered_map<AbstractState *, std::vector<int>> state_to_incoming_transitions;
    std::unordered_map<AbstractState *, std::vector<int>> state_to_outgoing_transitions;
    std::unordered_set<AbstractState *> states;
    AbstractState *initial_state;
    std::unordered_set<AbstractState *> goals;

public:
    explicit OCPConstraints(const Abstraction &abstraction);
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
