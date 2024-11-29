#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/markup.h"

using namespace std;

namespace cost_saturation {
CostPartitioningHeuristic compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    vector<int> &remaining_costs,
    const vector<int> &) {
    assert(abstractions.size() == order.size());
    CostPartitioningHeuristic cp_heuristic;
    for (int pos : order) {
        const Abstraction &abstraction = *abstractions[pos];
        vector<int> h_values = abstraction.compute_goal_distances(remaining_costs);
        vector<int> saturated_costs = abstraction.compute_saturated_costs(h_values);
        cp_heuristic.add_h_values(pos, move(h_values));
        reduce_costs(remaining_costs, saturated_costs);
    }
    return cp_heuristic;
}

static void cap_h_values(int h_cap, vector<int> &h_values) {
    assert(h_cap != -INF);
    for (int &h : h_values) {
        if (h != INF) {
            h = min(h, h_cap);
        }
    }
}

CostPartitioningHeuristic compute_perim_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    vector<int> &remaining_costs,
    const vector<int> &abstract_state_ids) {
    assert(abstractions.size() == order.size());
    CostPartitioningHeuristic cp_heuristic;
    for (int pos : order) {
        const Abstraction &abstraction = *abstractions[pos];
        vector<int> h_values = abstraction.compute_goal_distances(remaining_costs);
        int h_cap = h_values[abstract_state_ids[pos]];
        cap_h_values(h_cap, h_values);
        vector<int> saturated_costs = abstraction.compute_saturated_costs(h_values);
        cp_heuristic.add_h_values(pos, move(h_values));
        reduce_costs(remaining_costs, saturated_costs);
    }
    return cp_heuristic;
}

static CostPartitioningHeuristic compute_perimstar_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    vector<int> &remaining_costs,
    const vector<int> &abstract_state_ids) {
    CostPartitioningHeuristic cp = compute_perim_saturated_cost_partitioning(
        abstractions, order, remaining_costs, abstract_state_ids);
    cp.add(compute_saturated_cost_partitioning(
               abstractions, order, remaining_costs, abstract_state_ids));
    return cp;
}

void add_saturator_option(plugins::Feature &feature) {
    feature.add_option<Saturator>(
        "saturator",
        "function that computes saturated cost functions",
        "all");
}

CPFunction get_cp_function_from_options(const plugins::Options &options) {
    Saturator saturator_type = options.get<Saturator>("saturator");
    CPFunction cp_function = nullptr;
    if (saturator_type == Saturator::ALL) {
        cp_function = compute_saturated_cost_partitioning;
    } else if (saturator_type == Saturator::PERIM) {
        cp_function = compute_perim_saturated_cost_partitioning;
    } else if (saturator_type == Saturator::PERIMSTAR) {
        cp_function = compute_perimstar_saturated_cost_partitioning;
    } else {
        ABORT("Invalid value for saturator.");
    }
    return cp_function;
}

class SaturatedCostPartitioningHeuristicFeature
    : public plugins::TypedFeature<Evaluator, MaxCostPartitioningHeuristic> {
public:
    SaturatedCostPartitioningHeuristicFeature() : TypedFeature("scp") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Saturated cost partitioning");
        document_synopsis(
            "Compute the maximum over multiple saturated cost partitioning "
            "heuristics using different orders. For details, see " +
            utils::format_journal_reference(
                {"Jendrik Seipp", "Thomas Keller", "Malte Helmert"},
                "Saturated Cost Partitioning for Optimal Classical Planning",
                "https://ai.dmi.unibas.ch/papers/seipp-et-al-jair2020.pdf",
                "Journal of Artificial Intelligence Research",
                "67",
                "129-167",
                "2020"));
        document_note(
            "Difference to cegar()",
            "The cegar() plugin computes a single saturated cost partitioning over "
            "Cartesian abstraction heuristics. In contrast, "
            "saturated_cost_partitioning() supports computing the maximum over "
            "multiple saturated cost partitionings using different heuristic "
            "orders, and it supports both Cartesian abstraction heuristics and "
            "pattern database heuristics. While cegar() interleaves abstraction "
            "computation with cost partitioning, saturated_cost_partitioning() "
            "computes all abstractions using the original costs.");
        add_options_for_cost_partitioning_heuristic(*this, "scp");
        add_saturator_option(*this);
        add_order_options(*this);
    }

    virtual shared_ptr<MaxCostPartitioningHeuristic> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        shared_ptr<AbstractTask> task = options.get<shared_ptr<AbstractTask>>("transform");
        TaskProxy task_proxy(*task);
        vector<int> costs = task_properties::get_operator_costs(task_proxy);
        unique_ptr<DeadEnds> dead_ends = utils::make_unique_ptr<DeadEnds>();
        Abstractions abstractions = generate_abstractions(
            task, options.get_list<shared_ptr<AbstractionGenerator>>("abstractions"), dead_ends.get());
        CPFunction cp_function = get_cp_function_from_options(options);
        vector<CostPartitioningHeuristic> cp_heuristics =
            get_cp_heuristic_collection_generator_from_options(options)->generate_cost_partitionings(
                task_proxy, abstractions, costs, cp_function);
        return plugins::make_shared_from_arg_tuples<MaxCostPartitioningHeuristic>(
            move(abstractions),
            move(cp_heuristics),
            move(dead_ends),
            get_heuristic_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<SaturatedCostPartitioningHeuristicFeature> _plugin;

static plugins::TypedEnumPlugin<Saturator> _enum_plugin({
        {"all", "preserve estimates of all states"},
        {"perim", "preserve estimates of states in perimeter around goal"},
        {"perimstar", "compute 'perim' first and then 'all' with remaining costs"},
    });
}
