#include "cost_partitioning_generator_random.h"

#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/logging.h"
#include "../utils/rng.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
CostPartitioningGeneratorRandom::CostPartitioningGeneratorRandom(
    const Options &opts)
    : CostPartitioningGenerator(opts) {
}

void CostPartitioningGeneratorRandom::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &) {
    order = get_default_order(abstractions.size());
}

CostPartitioning CostPartitioningGeneratorRandom::get_next_cost_partitioning(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    rng->shuffle(order);
    if (max_orders == 1) {
        cout << "Order: " << order << endl;
    }
    return cp_function(abstractions, order, costs);
}

static shared_ptr<CostPartitioningGenerator> _parse_random(OptionParser &parser) {
    add_common_scp_generator_options_to_parser(parser);
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<CostPartitioningGeneratorRandom>(opts);
}

static PluginShared<CostPartitioningGenerator> _plugin_random(
    "random", _parse_random);
}
