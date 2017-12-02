#include "zero_one_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_collection_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/logging.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
static CostPartitionedHeuristic compute_zero_one_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool filter_blind_heuristics) {
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
            pos, abstraction.compute_h_values(remaining_costs), filter_blind_heuristics);
        for (int op_id : abstraction.get_active_operators()) {
            remaining_costs[op_id] = 0;
        }
    }
    return cp_heuristic;
}

ZeroOneCostPartitioningHeuristic::ZeroOneCostPartitioningHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts) {
    vector<int> costs = get_operator_costs(task_proxy);

    CostPartitioningCollectionGenerator cps_generator(
        opts.get<shared_ptr<CostPartitioningGenerator>>("orders"),
        opts.get<int>("max_orders"),
        opts.get<double>("max_time"),
        opts.get<bool>("diversify"),
        utils::parse_rng_from_options(opts));
    cp_heuristics =
        cps_generator.get_cost_partitionings(
            task_proxy, abstractions, costs,
            compute_zero_one_cost_partitioning);

    for (auto &abstraction : abstractions) {
        abstraction->release_transition_system_memory();
    }
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Zero-one cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_cost_partitioning_collection_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return new ZeroOneCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("zero_one_cost_partitioning", _parse);
}
