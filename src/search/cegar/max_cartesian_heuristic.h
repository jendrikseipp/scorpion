#ifndef CEGAR_MAX_CARTESIAN_HEURISTIC_H
#define CEGAR_MAX_CARTESIAN_HEURISTIC_H

#include "../heuristic.h"

#include <vector>

namespace cegar {
class Abstraction;
class Node;
class RefinementHierarchy;

/*
  Compute maximum over a set of additive cost partitionings.
*/
class MaxCartesianHeuristic : public Heuristic {
    using HMap = std::unordered_map<const Node *, int>;

    std::vector<std::shared_ptr<AbstractTask>> subtasks;
    std::vector<std::shared_ptr<RefinementHierarchy>> refinement_hierarchies;
    std::vector<std::vector<std::unordered_map<const Node *, int>>> h_maps;

    std::vector<HMap> create_additive_h_maps(
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &order) const;

    int compute_sum(
        const std::vector<State> &local_states,
        const std::vector<HMap> &order_h_maps) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state);
    int compute_heuristic(const State &state);

public:
    MaxCartesianHeuristic(
        const options::Options &opts,
        std::vector<std::unique_ptr<Abstraction>> &&abstractions,
        int num_orders);
};
}

#endif
