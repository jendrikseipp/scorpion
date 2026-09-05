#include "order_generator.h"

#include "../plugins/plugin.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
OrderGenerator::OrderGenerator(
    const shared_ptr<AbstractTask> &task, int random_seed)
    : components::TaskSpecificComponent(task),
      rng(utils::get_rng(random_seed)) {
}

void add_order_generator_arguments_to_feature(plugins::Feature &feature) {
    utils::add_rng_options_to_feature(feature);
}

tuple<int> get_order_generator_arguments_from_options(
    const plugins::Options &opts) {
    return tuple_cat(make_tuple(opts.get<int>("random_seed")));
}

static class OrderGeneratorCategoryPlugin
    : public plugins::TypedCategoryPlugin<TaskIndependentOrderGenerator> {
public:
    OrderGeneratorCategoryPlugin() : TypedCategoryPlugin("OrderGenerator") {
        document_synopsis(
            "Order abstractions for saturated cost partitioning.");
    }
} _category_plugin;
}
