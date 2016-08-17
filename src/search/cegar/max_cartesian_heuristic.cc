#include "max_cartesian_heuristic.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "scp_optimizer.h"
#include "utils.h"

#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
MaxCartesianHeuristic::MaxCartesianHeuristic(
    const options::Options &opts,
    vector<shared_ptr<RefinementHierarchy>> &&refinement_hierarchies,
    vector<vector<vector<int>>> &&h_values_by_orders)
    : Heuristic(opts),
      refinement_hierarchies(move(refinement_hierarchies)),
      h_values_by_orders(move(h_values_by_orders)) {
}

int MaxCartesianHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(
        refinement_hierarchies, state);
    int max_h = compute_max_h(local_state_ids, h_values_by_orders);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int MaxCartesianHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}
}
