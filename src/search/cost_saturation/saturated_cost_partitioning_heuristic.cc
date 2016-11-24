#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_generator.h"
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
    const bool verbose = debug;
    const vector<int> costs = get_operator_costs(task_proxy);

    if (opts.contains("orders")) {
        // Use orders provided by cost partitioning generator.
        h_values_by_order =
            opts.get<shared_ptr<CostPartitioningGenerator>>("orders")->get_cost_partitionings(
                task_proxy, abstractions, costs,
                [verbose](const Abstractions &abstractions, const vector<int> &order, const vector<int> &costs) {
                    return compute_saturated_cost_partitioning(abstractions, order, costs, verbose);
            });
    } else {
        int original_seed = rng->get_last_seed();

        // Shuffle abstractions from different generators separately.
        vector<vector<int>> suborders;
        int cumulative_num_abstractions = 0;
        for (int num_abstractions : abstractions_per_generator) {
            vector<int> suborder(num_abstractions);
            iota(suborder.begin(), suborder.end(), cumulative_num_abstractions);
            rng->seed(original_seed);
            rng->shuffle(suborder);
            suborders.push_back(move(suborder));
            cumulative_num_abstractions += num_abstractions;
        }
        cout << "Suborders: " << suborders << endl;

        // Loop over all permutations of suborders and concatenate suborders.
        sort(suborders.begin(), suborders.end());
        do {
            vector<int> random_order;
            for (const vector<int> &suborder : suborders) {
                random_order.insert(random_order.end(), suborder.begin(), suborder.end());
            }
            cout << "Order: " << random_order << endl;
            h_values_by_order.push_back(
                compute_saturated_cost_partitioning(
                    abstractions, random_order, costs, debug));
        } while (next_permutation(suborders.begin(), suborders.end()));
    }
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
