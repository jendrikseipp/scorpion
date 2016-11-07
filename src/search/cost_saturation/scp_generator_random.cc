#include "scp_generator_random.h"

#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/logging.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
SCPGeneratorRandom::SCPGeneratorRandom(const Options &opts)
    : SCPGenerator(opts),
      rng(utils::parse_rng_from_options(opts)) {
}

void SCPGeneratorRandom::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &) {
    order = get_default_order(abstractions.size());
}

CostPartitioning SCPGeneratorRandom::get_next_cost_partitioning(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    rng->shuffle(order);
    if (max_orders == 1) {
        cout << "Order: " << order << endl;
    }
    return compute_saturated_cost_partitioning(abstractions, order, costs);
}

static shared_ptr<SCPGenerator> _parse_random(OptionParser &parser) {
    add_common_scp_generator_options_to_parser(parser);
    utils::add_rng_options(parser);
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<SCPGeneratorRandom>(opts);
}

static PluginShared<SCPGenerator> _plugin_random(
    "random", _parse_random);
}
