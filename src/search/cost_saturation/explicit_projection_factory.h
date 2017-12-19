#ifndef COST_SATURATION_EXPLICIT_PROJECTION_FACTORY_H
#define COST_SATURATION_EXPLICIT_PROJECTION_FACTORY_H

#include "../task_proxy.h"

#include "../algorithms/ordered_set.h"
#include "../pdbs/types.h"

#include <vector>

namespace cost_saturation {
class Abstraction;
class Transition;

class ExplicitProjectionFactory {
    const TaskProxy task_proxy;
    const pdbs::Pattern pattern;

    std::vector<std::vector<Transition>> backward_graph;
    ordered_set::OrderedSet<int> looping_operators;
    std::vector<int> goal_states;
    int num_operators;

    // size of the PDB
    int num_states;

    // multipliers for each variable for perfect hash function
    std::vector<int> hash_multipliers;

    std::vector<int> compute_goal_states() const;

    void handle_operator(const OperatorProxy &op);

    /*
      For a given abstract state (given as index), the according values
      for each variable in the state are computed and compared with the
      given pairs of goal variables and values. Returns true iff the
      state is a goal state.
    */
    bool is_goal_state(
        int state_index,
        const std::vector<FactPair> &abstract_goals,
        const VariablesProxy &variables) const;

    /*
      The given concrete state is used to calculate the index of the
      according abstract state. This is only used for table lookup
      (distances) during search.
    */
    int hash_index(const State &state) const;

public:
    ExplicitProjectionFactory(const TaskProxy &task_proxy, const pdbs::Pattern &pattern);

    std::unique_ptr<Abstraction> convert_to_abstraction();
};
}

#endif
