#include "zero_one_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace cost_saturation {
static CostPartitionedHeuristic compute_zero_one_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool sparse) {
    assert(abstractions.size() == order.size());
    bool debug = false;

    vector<int> remaining_costs = costs;

    CostPartitionedHeuristic cp_heuristic;
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
        cp_heuristic.add_cp_heuristic_values(
            pos, abstraction.compute_h_values(remaining_costs), sparse);
        for (int op_id : abstraction.get_active_operators()) {
            remaining_costs[op_id] = 0;
        }
    }
    return cp_heuristic;
}

static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Zero-one cost partitioning heuristic",
        "");
    return get_max_cp_heuristic(parser, compute_zero_one_cost_partitioning);
}

static Plugin<Heuristic> _plugin("zero_one_cost_partitioning", _parse);
}
