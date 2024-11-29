#ifndef CARTESIAN_ABSTRACTIONS_ADDITIVE_CARTESIAN_HEURISTIC_H
#define CARTESIAN_ABSTRACTIONS_ADDITIVE_CARTESIAN_HEURISTIC_H

#include "../heuristic.h"

#include <vector>

namespace cartesian_abstractions {
class CartesianHeuristicFunction;
class SubtaskGenerator;
enum class DotGraphVerbosity;
enum class PickFlawedAbstractState;
enum class PickSplit;

/*
  Store CartesianHeuristicFunctions and compute overall heuristic by
  summing all of their values.
*/
class AdditiveCartesianHeuristic : public Heuristic {
    const std::vector<CartesianHeuristicFunction> heuristic_functions;

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    explicit AdditiveCartesianHeuristic(
        const std::vector<std::shared_ptr<SubtaskGenerator>> &subtasks,
        int max_states, int max_transitions, double max_time,
        PickFlawedAbstractState pick_flawed_abstract_state,
        PickSplit pick_split, PickSplit tiebreak_split,
        int max_concrete_states_per_abstract_state, int max_state_expansions,
        int memory_padding, int random_seed, DotGraphVerbosity dot_graph_verbosity,
        bool use_general_costs,
        const std::shared_ptr<AbstractTask> &transform,
        bool cache_estimates, const std::string &description, utils::Verbosity verbosity);
};
}

#endif
