#include "cost_partitioning_heuristic.h"

#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
class AbstractionGenerator;

CostPartitioningHeuristic::CostPartitioningHeuristic(const Options &opts)
    : Heuristic(opts),
      debug(opts.get<bool>("debug")) {
}

int CostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int CostPartitioningHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(state_maps, state);
    int max_h = compute_max_h_with_statistics(local_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int CostPartitioningHeuristic::compute_max_h_with_statistics(
    const vector<int> &local_state_ids) const {
    int max_h = 0;
    int best_id = -1;
    int current_id = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        if (sum_h > max_h) {
            max_h = sum_h;
            best_id = current_id;
        }
        if (sum_h == INF) {
            break;
        }
        ++current_id;
    }
    assert(max_h >= 0);

    if (best_id != -1 && !num_best_order.empty()) {
        assert(utils::in_bounds(best_id, num_best_order));
        ++num_best_order[best_id];
    }

    return max_h;
}

void CostPartitioningHeuristic::print_statistics() const {
    int num_superfluous = count(num_best_order.begin(), num_best_order.end(), 0);
    int num_orders = num_best_order.size();
    double percentage_superfluous =
        (num_orders == 0) ? 0 : num_superfluous * 100.0 / num_orders;
    cout << "Number of times each order was the best order: "
         << num_best_order << endl;
    cout << "Superfluous orders: " << num_superfluous << "/" << num_orders
         << " = " << percentage_superfluous << endl;
}

void add_common_cost_partitioning_options_to_parser(
    options::OptionParser &parser) {
    parser.add_list_option<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators",
        "methods that generate abstractions");
    parser.add_option<bool>(
        "debug",
        "print debugging information",
        "false");
    Heuristic::add_options_to_parser(parser);
}
}
