#include "zero_one_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"

using namespace std;

namespace cost_saturation {
static CostPartitioningHeuristic compute_zero_one_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order, vector<int> &remaining_costs,
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

ZeroOneCostPartitioningHeuristic::ZeroOneCostPartitioningHeuristic(
    const shared_ptr<AbstractTask> &task,
    const vector<shared_ptr<AbstractionGenerator>> &abstraction_generators,
    const shared_ptr<OrderGenerator> &order_generator, int max_orders,
    int max_size_kb, double max_time, bool diversify, int num_samples,
    double max_optimization_time, int random_seed, bool cache_estimates,
    const string &description, utils::Verbosity verbosity)
    : MaxCostPartitioningHeuristic(
          task, cache_estimates, description, verbosity) {
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    unique_ptr<DeadEnds> dead_ends = make_unique<DeadEnds>();
    Abstractions abstractions =
        generate_abstractions(task, abstraction_generators, dead_ends.get());
    CPHeuristics cp_heuristics = compute_cp_heuristics(
        task_proxy, abstractions, costs, compute_zero_one_cost_partitioning,
        order_generator, max_orders, max_size_kb, max_time, diversify,
        num_samples, max_optimization_time, random_seed);
    set_cost_partitionings(
        move(abstractions), move(cp_heuristics), move(dead_ends));
}

class ZeroOneCostPartitioningHeuristicFeature
    : public plugins::TypedFeature<TaskIndependentEvaluator> {
public:
    ZeroOneCostPartitioningHeuristicFeature() : TypedFeature("gzocp") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Greedy zero-one cost partitioning");
        add_options_for_cost_partitioning_heuristic(*this, "gzocp");
        add_order_options(*this);
    }

    virtual shared_ptr<TaskIndependentEvaluator> create_component(
        const plugins::Options &options) const override {
        return components::make_auto_task_independent_component<
            ZeroOneCostPartitioningHeuristic, Evaluator>(
            get_abstraction_generator_list_from_options(options),
            get_order_arguments_from_options(options),
            get_heuristic_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<ZeroOneCostPartitioningHeuristicFeature> _plugin;
}
