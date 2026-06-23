#ifndef COST_SATURATION_CARTESIAN_ABSTRACTION_H
#define COST_SATURATION_CARTESIAN_ABSTRACTION_H

#include "abstraction.h"

#include "../cartesian_abstractions/refinement_hierarchy.h"

#include <memory>
#include <vector>

namespace cartesian_abstractions {
class Abstraction;
}

namespace cost_saturation {
class CartesianAbstractionFunction : public AbstractionFunction {
    std::unique_ptr<cartesian_abstractions::RefinementHierarchy>
        refinement_hierarchy;

public:
    explicit CartesianAbstractionFunction(
        std::unique_ptr<cartesian_abstractions::RefinementHierarchy>
            refinement_hierarchy)
        : refinement_hierarchy(move(refinement_hierarchy)) {
    }

    virtual int get_abstract_state_id(
        const State &concrete_state) const override {
        return refinement_hierarchy->get_abstract_state_id(concrete_state);
    }
};

class CartesianAbstraction : public Abstraction {
    std::unique_ptr<cartesian_abstractions::Abstraction> abstraction;

    // Operators inducing state-changing transitions.
    std::vector<bool> active_operators;

    // Operators inducing self-loops.
    std::vector<bool> looping_operators;

    std::vector<int> goal_states;

public:
    explicit CartesianAbstraction(
        std::unique_ptr<cartesian_abstractions::Abstraction> &&abstraction);

    virtual std::vector<int> compute_goal_distances(
        const std::vector<int> &costs) const override;
    virtual std::vector<int> compute_saturated_costs(
        const std::vector<int> &h_values) const override;
    virtual int get_num_operators() const override;
    virtual bool operator_is_active(int op_id) const override;
    virtual bool operator_induces_self_loop(int op_id) const override;
    virtual void for_each_transition(
        const TransitionCallback &callback) const override;
    virtual int get_num_states() const override;
    virtual const std::vector<int> &get_goal_states() const override;
    virtual void dump() const override;
};
}

#endif
