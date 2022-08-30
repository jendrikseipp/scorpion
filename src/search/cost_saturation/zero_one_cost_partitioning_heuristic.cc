#include "zero_one_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace cost_saturation {
static CostPartitioningHeuristic compute_zero_one_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    vector<int> &remaining_costs,
    const vector<int> &) {
    assert(abstractions.size() == order.size());
    bool debug = false;

    CostPartitioningHeuristic cp_heuristic;
    for (int pos : order) {
        const Abstraction &abstraction = *abstractions[pos];
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
        cp_heuristic.add_h_values(
            pos, abstraction.compute_goal_distances(remaining_costs));
        for (size_t op_id = 0; op_id < remaining_costs.size(); ++op_id) {
            if (abstraction.operator_is_active(op_id)) {
                remaining_costs[op_id] = 0;
            }
        }
    }
    return cp_heuristic;
}

static shared_ptr<Evaluator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Greedy zero-one cost partitioning",
        "");
    return get_max_cp_heuristic(parser, compute_zero_one_cost_partitioning);
}

static Plugin<Evaluator> _plugin("gzocp", _parse, "heuristics_cost_partitioning");
}
