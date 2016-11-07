#ifndef COST_SATURATION_ABSTRACTION_H
#define COST_SATURATION_ABSTRACTION_H

#include <vector>

class State;

namespace cost_saturation {
class Abstraction {
protected:
    const bool use_general_costs;

    virtual std::vector<int> compute_saturated_costs(
        const std::vector<int> &h_values) const = 0;

public:
    Abstraction();
    virtual ~Abstraction();

    Abstraction(const Abstraction &) = delete;

    virtual int get_abstract_state_id(const State &concrete_state) const = 0;

    virtual std::vector<int> compute_h_values(
        const std::vector<int> &costs) const = 0;

    std::pair<std::vector<int>, std::vector<int>>
        compute_goal_distances_and_saturated_costs(
            const std::vector<int> &costs) const;

    virtual const std::vector<int> &get_active_operators() const = 0;

    virtual int get_num_states() const = 0;

    virtual void dump() const = 0;
};
}

#endif
