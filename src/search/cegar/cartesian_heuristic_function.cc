#include "cartesian_heuristic_function.h"

using namespace std;

namespace cegar {
CartesianHeuristicFunction::CartesianHeuristicFunction(
    const std::shared_ptr<AbstractTask> &task,
    RefinementHierarchy &&hierarchy,
    std::unordered_map<const Node *, int> &&h_map)
    : task(task),
      task_proxy(*task),
      refinement_hierarchy(move(hierarchy)),
      h_map(move(h_map)) {
}

int CartesianHeuristicFunction::get_value(const State &parent_state) const {
    State local_state = task_proxy.convert_ancestor_state(parent_state);
    return h_map.at(refinement_hierarchy.get_node(local_state));
}
}
