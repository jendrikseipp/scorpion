#include "order_generator_random.h"

#include "utils.h"

#include "../plugins/plugin.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

using namespace std;

namespace cost_saturation {
OrderGeneratorRandom::OrderGeneratorRandom(int random_seed) :
    OrderGenerator(random_seed) {
}

void OrderGeneratorRandom::initialize(
    const Abstractions &abstractions,
    const vector<int> &) {
    utils::g_log << "Initialize random order generator" << endl;
    random_order = get_default_order(abstractions.size());
}

Order OrderGeneratorRandom::compute_order_for_state(
    const vector<int> &,
    bool) {
    rng->shuffle(random_order);
    return random_order;
}

class OrderGeneratorRandomFeature
    : public plugins::TypedFeature<OrderGenerator, OrderGeneratorRandom> {
public:
    OrderGeneratorRandomFeature() : TypedFeature("random_orders") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Random orders");
        document_synopsis("Shuffle abstractions randomly.");
        add_order_generator_arguments_to_feature(*this);
    }

    virtual shared_ptr<OrderGeneratorRandom> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<OrderGeneratorRandom>(
            get_order_generator_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<OrderGeneratorRandomFeature> _plugin;
}
