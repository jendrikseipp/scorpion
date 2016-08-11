#ifndef CEGAR_CARTESIAN_HEURISTIC_FUNCTION_H
#define CEGAR_CARTESIAN_HEURISTIC_FUNCTION_H

#include "../task_proxy.h"

#include <memory>
#include <unordered_map>

class AbstractTask;
class State;

namespace cegar {
class Node;
class RefinementHierarchy;

/*
  Store RefinementHierarchy and subtask for looking up heuristic values
  efficiently.
*/
class CartesianHeuristicFunction {
    const std::shared_ptr<AbstractTask> task;
    TaskProxy task_proxy;
    std::shared_ptr<RefinementHierarchy> refinement_hierarchy;
    std::unordered_map<const Node *, int> h_map;

public:
    CartesianHeuristicFunction(
        const std::shared_ptr<AbstractTask> &task,
        const std::shared_ptr<RefinementHierarchy> &hierarchy,
            std::unordered_map<const Node *, int> &&h_map = {});

    int get_value(const State &parent_state) const;

    int get_abstract_state_id(const State &parent_state) const;
};
}

#endif
