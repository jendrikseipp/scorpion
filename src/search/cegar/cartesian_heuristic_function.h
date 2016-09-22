#ifndef CEGAR_CARTESIAN_HEURISTIC_FUNCTION_H
#define CEGAR_CARTESIAN_HEURISTIC_FUNCTION_H

#include <memory>
#include <vector>

class State;

namespace cegar {
class RefinementHierarchy;

/*
  Store RefinementHierarchy and subtask for looking up heuristic values
  efficiently.
*/
class CartesianHeuristicFunction {
    std::shared_ptr<RefinementHierarchy> refinement_hierarchy;
    std::vector<int> h_values;

public:
    CartesianHeuristicFunction(
        const std::shared_ptr<RefinementHierarchy> &hierarchy,
        std::vector<int> &&h_values);

    // Visual Studio 2013 needs an explicit implementation.
    CartesianHeuristicFunction(CartesianHeuristicFunction &&other)
        : refinement_hierarchy(std::move(other.refinement_hierarchy)),
          h_values(std::move(other.h_values)) {
    }

    int get_value(const State &parent_state) const;
};
}

#endif
