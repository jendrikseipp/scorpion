#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "scp_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/logging.h"

#include <algorithm>

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningHeuristic::SaturatedCostPartitioningHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts) {
    const vector<shared_ptr<AbstractionGenerator>> abstraction_generators =
        opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators");
    const bool interleave_abstractions = opts.get<bool>("interleave_abstractions");

    if (!interleave_abstractions && abstraction_generators.size() == 1) {
        cerr << "interleave_abstractions=false is only supported when using "
                "multiple abstraction generators" << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }

    vector<unique_ptr<Abstraction>> abstractions;
    vector<int> abstractions_per_generator;
    for (const shared_ptr<AbstractionGenerator> &generator : abstraction_generators) {
        int abstractions_before = abstractions.size();
        for (AbstractionAndStateMap &pair : generator->generate_abstractions(task)) {
            abstractions.push_back(move(pair.first));
            state_maps.push_back(move(pair.second));
        }
        abstractions_per_generator.push_back(abstractions.size() - abstractions_before);
    }
    cout << "Abstractions: " << abstractions.size() << endl;
    cout << "Abstractions per generator: " << abstractions_per_generator << endl;

    utils::Timer scp_timer;
    const vector<int> costs = get_operator_costs(task_proxy);


    if (interleave_abstractions) {
        h_values_by_order =
            opts.get<shared_ptr<SCPGenerator>>("orders")->get_cost_partitionings(
                task_proxy, abstractions, state_maps, costs);
    } else {
        vector<int> random_order;
        for (int num_abstractions : abstractions_per_generator) {
            vector<int> suborder(num_abstractions);
            iota(suborder.begin(), suborder.end(), random_order.size());
            g_rng()->shuffle(suborder);
            random_order.insert(random_order.end(), suborder.begin(), suborder.end());
        }
        cout << "Order: " << random_order << endl;
        h_values_by_order = {
            compute_saturated_cost_partitioning(
                abstractions, random_order, costs)};
    }
    num_best_order.resize(h_values_by_order.size(), 0);

    cout << "Time for computing cost partitionings: " << scp_timer << endl;
    cout << "Orders: " << h_values_by_order.size() << endl;
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    parser.add_option<shared_ptr<SCPGenerator>>(
        "orders",
        "saturated cost partitioning generator",
        "random(1)");
    parser.add_option<bool>(
        "interleave_abstractions",
        "allow interleaving abstractions from different generators",
        "true");

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
