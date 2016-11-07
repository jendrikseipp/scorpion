#include "zero_one_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
static vector<vector<int>> compute_zero_one_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool debug) {
    assert(abstractions.size() == order.size());

    vector<int> remaining_costs = costs;

    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
        h_values_by_abstraction[pos] = abstraction.compute_h_values(remaining_costs);
        for (int op_id : abstraction.get_active_operators()) {
            remaining_costs[op_id] = 0;
        }
    }
    return h_values_by_abstraction;
}

ZeroOneCostPartitioningHeuristic::ZeroOneCostPartitioningHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts) {
    vector<unique_ptr<Abstraction>> abstractions;
    for (const shared_ptr<AbstractionGenerator> &generator :
         opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators")) {
        for (unique_ptr<Abstraction> &abstraction : generator->generate_abstractions(task)) {
            abstractions.push_back(move(abstraction));
        }
    }
    cout << "Abstractions: " << abstractions.size() << endl;
    if (debug) {
        for (const unique_ptr<Abstraction> &abstraction : abstractions) {
            abstraction->dump();
        }
    }

    utils::Timer timer;
    vector<int> costs = get_operator_costs(task_proxy);

    vector<int> random_order = get_default_order(abstractions.size());
    rng->shuffle(random_order);
    cout << "Order: " << random_order << endl;
    h_values_by_order = {
        compute_zero_one_cost_partitioning(
            abstractions, random_order, costs, debug)};

    cout << "Time for computing cost partitionings: " << timer << endl;
    cout << "Orders: " << h_values_by_order.size() << endl;
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Zero-one cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    opts.verify_list_non_empty<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators");

    if (parser.dry_run())
        return nullptr;

    return new ZeroOneCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("zero_one_cost_partitioning", _parse);
}
