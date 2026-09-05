#include "order_generator_random.h"

#include "utils.h"

#include "../plugins/plugin.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

using namespace std;

namespace cost_saturation {
OrderGeneratorRandom::OrderGeneratorRandom(
    const shared_ptr<AbstractTask> &task, int random_seed)
    : OrderGenerator(task, random_seed) {
}

void OrderGeneratorRandom::initialize(
    const Abstractions &abstractions, const vector<int> &) {
    utils::g_log << "Initialize random order generator" << endl;
    random_order = get_default_order(abstractions.size());
}

Order OrderGeneratorRandom::compute_order_for_state(const vector<int> &, bool) {
    rng->shuffle(random_order);
    return random_order;
}

class OrderGeneratorRandomFeature
    : public plugins::TypedFeature<TaskIndependentOrderGenerator> {
public:
    OrderGeneratorRandomFeature() : TypedFeature("random_orders") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Random orders");
        document_synopsis("Shuffle abstractions randomly.");
        add_order_generator_arguments_to_feature(*this);
    }

    virtual shared_ptr<TaskIndependentOrderGenerator> create_component(
        const plugins::Options &options) const override {
        return components::make_auto_task_independent_component<
            OrderGeneratorRandom, OrderGenerator>(
            get_order_generator_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<OrderGeneratorRandomFeature> _plugin;
}
