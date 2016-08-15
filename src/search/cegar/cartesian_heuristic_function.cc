#include "cartesian_heuristic_function.h"

#include "refinement_hierarchy.h"

#include "../utils/collections.h"

#include <cassert>

using namespace std;

namespace cegar {
CartesianHeuristicFunction::CartesianHeuristicFunction(
    const shared_ptr<RefinementHierarchy> &hierarchy,
    std::vector<int> &&h_values)
    : refinement_hierarchy(hierarchy),
      h_values(move(h_values)) {
}

int CartesianHeuristicFunction::get_value(const State &parent_state) const {
    int state_id = refinement_hierarchy->get_local_state_id(parent_state);
    assert(utils::in_bounds(state_id, h_values));
    return h_values[state_id];
}
}
