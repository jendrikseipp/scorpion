#ifndef CEGAR_CARTESIAN_HEURISTIC_FUNCTION_H
#define CEGAR_CARTESIAN_HEURISTIC_FUNCTION_H

#include "refinement_hierarchy.h"

#include "../task_proxy.h"

#include <memory>
#include <unordered_map>

class AbstractTask;
class State;

namespace cegar {
class CartesianHeuristicFunction {
    const std::shared_ptr<AbstractTask> task;
    TaskProxy task_proxy;
    // Note: for move-semantics to work, this member can't be const.
    std::shared_ptr<RefinementHierarchy> refinement_hierarchy;
    std::unordered_map<const Node *, int> h_map;

public:
    CartesianHeuristicFunction(
        const std::shared_ptr<AbstractTask> &task,
        const std::shared_ptr<RefinementHierarchy> &hierarchy,
        std::unordered_map<const Node *, int> &&h_map);
    ~CartesianHeuristicFunction() = default;

    CartesianHeuristicFunction(CartesianHeuristicFunction &&other) = default;

    int get_value(const State &parent_state) const;
};
}

#endif
