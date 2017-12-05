#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_collection_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningHeuristic::SaturatedCostPartitioningHeuristic(
    const Options &opts)
    : CostPartitioningHeuristic(opts) {
    vector<int> costs = get_operator_costs(task_proxy);
    cp_heuristics =
        get_cp_collection_generator_from_options(opts).get_cost_partitionings(
            task_proxy, abstractions, costs, compute_saturated_cost_partitioning);

    int num_heuristics = abstractions.size() * cp_heuristics.size();
    int num_stored_heuristics = 0;
    for (const auto &cp_heuristic: cp_heuristics) {
        num_stored_heuristics += cp_heuristic.size();
    }
    cout << "Stored heuristics: " << num_stored_heuristics << "/"
         << num_heuristics << " = "
         << num_stored_heuristics / static_cast<double>(num_heuristics) << endl;

    for (auto &abstraction : abstractions) {
        abstraction->release_transition_system_memory();
    }
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_cost_partitioning_collection_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return new SaturatedCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("saturated_cost_partitioning", _parse);
}
