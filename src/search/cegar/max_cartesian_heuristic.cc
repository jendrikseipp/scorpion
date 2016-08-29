#include "max_cartesian_heuristic.h"

#include "scp_optimizer.h"
#include "utils.h"

#include "../utils/logging.h"

using namespace std;

namespace cegar {
MaxCartesianHeuristic::MaxCartesianHeuristic(
    const options::Options &opts,
    vector<shared_ptr<RefinementHierarchy>> &&refinement_hierarchies,
    vector<vector<vector<int>>> &&h_values_by_order)
    : Heuristic(opts),
      refinement_hierarchies(move(refinement_hierarchies)),
      h_values_by_order(move(h_values_by_order)),
      num_best_order(this->h_values_by_order.size(), 0) {
}

int MaxCartesianHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(
        refinement_hierarchies, state);
    int max_h = compute_max_h_with_statistics(local_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int MaxCartesianHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int MaxCartesianHeuristic::compute_max_h_with_statistics(
    const vector<int> &local_state_ids) {
    int max_h = -1;
    int best_id = -1;
    int current_id = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        if (sum_h == INF) {
            return INF;
        }
        if (sum_h > max_h) {
            max_h = sum_h;
            best_id = current_id;
        }
        ++current_id;
    }

    assert(utils::in_bounds(best_id, num_best_order));
    ++num_best_order[best_id];

    return max_h;
}

void MaxCartesianHeuristic::print_statistics() const {
    int num_superfluous = count(num_best_order.begin(), num_best_order.end(), 0);
    int num_orders = num_best_order.size();
    assert(num_orders != 0);
    cout << "Number of times each order was the best order: "
         << num_best_order << endl;
    cout << "Superfluous orders: " << num_superfluous << "/" << num_orders
         << " = " << num_superfluous * 100.0 / num_orders << endl;
}
}
