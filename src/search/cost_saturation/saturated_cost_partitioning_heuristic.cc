#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../algorithms/partial_state_tree.h"
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

void add_saturator_option(OptionParser &parser) {
    parser.add_enum_option<Saturator>(
        "saturator",
        {"all", "perim", "perimstar"},
        "function that computes saturated cost functions",
        "all",
        {"preserve estimates of all states",
         "preserve estimates of states in perimeter around goal",
         "compute 'perim' first and then 'all' with remaining costs"});
}

CPFunction get_cp_function_from_options(const Options &opts) {
    Saturator saturator_type = opts.get<Saturator>("saturator");
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

static shared_ptr<Evaluator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning",
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
    parser.document_note(
        "Difference to cegar()",
        "The cegar() plugin computes a single saturated cost partitioning over "
        "Cartesian abstraction heuristics. In contrast, "
        "saturated_cost_partitioning() supports computing the maximum over "
        "multiple saturated cost partitionings using different heuristic "
        "orders, and it supports both Cartesian abstraction heuristics and "
        "pattern database heuristics. While cegar() interleaves abstraction "
        "computation with cost partitioning, saturated_cost_partitioning() "
        "computes all abstractions using the original costs.");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_saturator_option(parser);
    add_order_options_to_parser(parser);

    options::Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    unique_ptr<DeadEnds> dead_ends = utils::make_unique_ptr<DeadEnds>();
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"), dead_ends.get());
    CPFunction cp_function = get_cp_function_from_options(opts);
    vector<CostPartitioningHeuristic> cp_heuristics =
        get_cp_heuristic_collection_generator_from_options(opts).generate_cost_partitionings(
            task_proxy, abstractions, costs, cp_function);
    return make_shared<MaxCostPartitioningHeuristic>(
        opts,
        move(abstractions),
        move(cp_heuristics),
        move(dead_ends));
}

static Plugin<Evaluator> _plugin("scp", _parse, "heuristics_cost_partitioning");
}
