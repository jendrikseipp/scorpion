#include "order_generator_dynamic_greedy.h"

#include "abstraction.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
OrderGeneratorDynamicGreedy::OrderGeneratorDynamicGreedy(
    ScoringFunction scoring_function, int random_seed)
    : OrderGenerator(random_seed),
      scoring_function(scoring_function),
      abstractions(nullptr),
      costs(nullptr) {
}

Order OrderGeneratorDynamicGreedy::compute_dynamic_greedy_order_for_sample(
    const vector<int> &abstract_state_ids,
    vector<int> remaining_costs) const {
    assert(abstractions->size() == abstract_state_ids.size());
    vector<int> remaining_abstractions = get_default_order(abstractions->size());

    Order order;
    while (!remaining_abstractions.empty()) {
        int num_remaining = remaining_abstractions.size();
        vector<int> current_h_values;
        vector<vector<int>> current_saturated_costs;
        current_h_values.reserve(num_remaining);
        current_saturated_costs.reserve(num_remaining);

        // Shuffle remaining abstractions to break ties randomly.
        rng->shuffle(remaining_abstractions);
        vector<int> saturated_costs_for_best_abstraction;
        for (int abs_id : remaining_abstractions) {
            assert(utils::in_bounds(abs_id, abstract_state_ids));
            int abstract_state_id = abstract_state_ids[abs_id];
            const Abstraction &abstraction = *abstractions->at(abs_id);
            vector<int> h_values = abstraction.compute_goal_distances(
                remaining_costs);
            vector<int> saturated_costs = abstraction.compute_saturated_costs(
                h_values);
            assert(utils::in_bounds(abstract_state_id, h_values));
            int h = h_values[abstract_state_id];
            current_h_values.push_back(h);
            current_saturated_costs.push_back(move(saturated_costs));
        }

        vector<int> surplus_costs = compute_all_surplus_costs(
            remaining_costs, current_saturated_costs);

        double highest_score = -numeric_limits<double>::max();
        int best_rem_id = -1;
        for (int rem_id = 0; rem_id < num_remaining; ++rem_id) {
            int used_costs = compute_costs_stolen_by_heuristic(
                current_saturated_costs[rem_id], surplus_costs);
            double score = compute_score(
                current_h_values[rem_id], used_costs, scoring_function);
            if (score > highest_score) {
                best_rem_id = rem_id;
                highest_score = score;
            }
        }
        assert(utils::in_bounds(best_rem_id, remaining_abstractions));
        order.push_back(remaining_abstractions[best_rem_id]);
        reduce_costs(remaining_costs, current_saturated_costs[best_rem_id]);
        utils::swap_and_pop_from_vector(remaining_abstractions, best_rem_id);
    }
    return order;
}

void OrderGeneratorDynamicGreedy::initialize(
    const Abstractions &abstractions_,
    const vector<int> &costs_) {
    utils::g_log << "Initialize dynamic greedy order generator" << endl;
    abstractions = &abstractions_;
    costs = &costs_;
}

Order OrderGeneratorDynamicGreedy::compute_order_for_state(
    const vector<int> &abstract_state_ids,
    bool verbose) {
    assert(abstractions && costs);
    utils::Timer greedy_timer;
    vector<int> order = compute_dynamic_greedy_order_for_sample(
        abstract_state_ids, *costs);

    if (verbose) {
        utils::g_log << "Time for computing dynamic greedy order: "
                     << greedy_timer << endl;
    }

    assert(order.size() == abstractions->size());
    return order;
}

class OrderGeneratorDynamicGreedyFeature
    : public plugins::TypedFeature<OrderGenerator, OrderGeneratorDynamicGreedy> {
public:
    OrderGeneratorDynamicGreedyFeature() : TypedFeature("dynamic_greedy_orders") {
        document_title("Dynamic greedy orders");
        document_synopsis(
            "Order abstractions greedily by a given scoring function, "
            "dynamically recomputing the next best abstraction after each ordering step.");
        add_scoring_function_to_feature(*this);
        add_order_generator_arguments_to_feature(*this);
    }

    virtual shared_ptr<OrderGeneratorDynamicGreedy> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<OrderGeneratorDynamicGreedy>(
            options.get<ScoringFunction>("scoring_function"),
            get_order_generator_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<OrderGeneratorDynamicGreedyFeature> _plugin;
}
