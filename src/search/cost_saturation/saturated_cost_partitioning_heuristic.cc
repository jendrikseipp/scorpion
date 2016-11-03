#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "scp_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningHeuristic::SaturatedCostPartitioningHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts) {
    vector<unique_ptr<Abstraction>> abstractions;
    for (const shared_ptr<AbstractionGenerator> &generator :
         opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators")) {
        for (AbstractionAndStateMap &pair : generator->generate_abstractions(task)) {
            abstractions.push_back(move(pair.first));
            state_maps.push_back(move(pair.second));
        }
    }
    cout << "Abstractions: " << abstractions.size() << endl;

    utils::Timer scp_timer;
    const vector<int> costs = get_operator_costs(task_proxy);

    h_values_by_order =
        opts.get<shared_ptr<SCPGenerator>>("orders")->get_cost_partitionings(
            task_proxy, abstractions, state_maps, costs);
    num_best_order.resize(h_values_by_order.size(), 0);

    cout << "Time for computing cost partitionings: " << scp_timer << endl;
    cout << "Orders: " << h_values_by_order.size() << endl;
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning heuristic",
        "");

    parser.document_language_support("action costs", "supported");
    parser.document_language_support(
        "conditional effects",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_language_support(
        "axioms",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_property("admissible", "yes");
    parser.document_property(
        "consistent",
        "yes, if all abstraction generators represent consistent heuristics");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    add_common_cost_partitioning_options_to_parser(parser);
    parser.add_option<shared_ptr<SCPGenerator>>(
        "orders",
        "saturated cost partitioning generator",
        "random(1)");

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    opts.verify_list_non_empty<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators");

    if (parser.dry_run())
        return nullptr;

    return new SaturatedCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("saturated_cost_partitioning", _parse);
}
