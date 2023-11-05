#include "order_generator.h"

#include "../plugins/plugin.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
OrderGenerator::OrderGenerator(const plugins::Options &opts)
    : rng(utils::parse_rng_from_options(opts)) {
}

void add_common_order_generator_options(plugins::Feature &feature) {
    utils::add_rng_options(feature);
}

static class OrderGeneratorCategoryPlugin : public plugins::TypedCategoryPlugin<OrderGenerator> {
public:
    OrderGeneratorCategoryPlugin() : TypedCategoryPlugin("OrderGenerator") {
        document_synopsis("Order abstractions for saturated cost partitioning.");
    }
}
_category_plugin;
}
