#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_generator.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningHeuristic::SaturatedCostPartitioningHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts) {
    const bool verbose = debug;

    vector<int> costs = get_operator_costs(task_proxy);
    h_values_by_order =
        opts.get<shared_ptr<CostPartitioningGenerator>>("orders")->get_cost_partitionings(
            task_proxy, abstractions, costs,
            [verbose](const Abstractions &abstractions, const vector<int> &order, const vector<int> &costs) {
            return compute_saturated_cost_partitioning(abstractions, order, costs, verbose);
        });
    num_best_order.resize(h_values_by_order.size(), 0);

    for (auto &abstraction : abstractions) {
        abstraction->release_transition_system_memory();
    }
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return new SaturatedCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("saturated_cost_partitioning", _parse);
}
