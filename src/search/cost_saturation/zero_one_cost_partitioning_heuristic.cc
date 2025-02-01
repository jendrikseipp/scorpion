#include "zero_one_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../plugins/plugin.h"

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

class ZeroOneCostPartitioningHeuristicFeature
    : public plugins::TypedFeature<Evaluator, MaxCostPartitioningHeuristic> {
public:
    ZeroOneCostPartitioningHeuristicFeature() : TypedFeature("gzocp") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Greedy zero-one cost partitioning");
        add_options_for_cost_partitioning_heuristic(*this, "gzocp");
        add_order_options(*this);
    }

    virtual shared_ptr<MaxCostPartitioningHeuristic> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return get_max_cp_heuristic(options, compute_zero_one_cost_partitioning);
    }
};

static plugins::FeaturePlugin<ZeroOneCostPartitioningHeuristicFeature> _plugin;
}
