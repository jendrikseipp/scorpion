#include "order_generator_greedy.h"

#include "abstraction.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <cassert>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
OrderGeneratorGreedy::OrderGeneratorGreedy(
    ScoringFunction scoring_function, int random_seed)
    : OrderGenerator(random_seed),
      scoring_function(scoring_function) {
}

double OrderGeneratorGreedy::rate_abstraction(
    const vector<int> &abstract_state_ids, int abs_id) const {
    assert(utils::in_bounds(abs_id, abstract_state_ids));
    int abstract_state_id = abstract_state_ids[abs_id];
    assert(utils::in_bounds(abs_id, h_values_by_abstraction));
    assert(utils::in_bounds(abstract_state_id, h_values_by_abstraction[abs_id]));
    int h = h_values_by_abstraction[abs_id][abstract_state_id];
    assert(h >= 0);

    assert(utils::in_bounds(abs_id, stolen_costs_by_abstraction));
    int stolen_costs = stolen_costs_by_abstraction[abs_id];

    return compute_score(h, stolen_costs, scoring_function);
}

void OrderGeneratorGreedy::initialize(
    const Abstractions &abstractions,
    const vector<int> &costs) {
    utils::Timer timer;
    utils::g_log << "Initialize greedy order generator" << endl;

    vector<vector<int>> saturated_costs_by_abstraction;
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        vector<int> h_values = abstraction->compute_goal_distances(costs);
        vector<int> saturated_costs = abstraction->compute_saturated_costs(h_values);
        h_values_by_abstraction.push_back(move(h_values));
        saturated_costs_by_abstraction.push_back(move(saturated_costs));
    }
    utils::g_log << "Time for computing h values and saturated costs: "
                 << timer << endl;

    vector<int> surplus_costs = compute_all_surplus_costs(
        costs, saturated_costs_by_abstraction);
    utils::g_log << "Done computing surplus costs" << endl;

    utils::g_log << "Compute stolen costs" << endl;
    int num_abstractions = abstractions.size();
    for (int abs = 0; abs < num_abstractions; ++abs) {
        int sum_stolen_costs = compute_costs_stolen_by_heuristic(
            saturated_costs_by_abstraction[abs], surplus_costs);
        stolen_costs_by_abstraction.push_back(sum_stolen_costs);
    }
    utils::g_log << "Time for initializing greedy order generator: "
                 << timer << endl;
}

Order OrderGeneratorGreedy::compute_order_for_state(
    const vector<int> &abstract_state_ids,
    bool verbose) {
    assert(abstract_state_ids.size() == h_values_by_abstraction.size());
    utils::Timer greedy_timer;
    int num_abstractions = abstract_state_ids.size();
    Order order = get_default_order(num_abstractions);
    // Shuffle order to break ties randomly.
    rng->shuffle(order);
    vector<double> scores;
    scores.reserve(num_abstractions);
    for (int abs = 0; abs < num_abstractions; ++abs) {
        scores.push_back(rate_abstraction(abstract_state_ids, abs));
    }
    sort(order.begin(), order.end(), [&](int abs1, int abs2) {
             return scores[abs1] > scores[abs2];
         });

    if (verbose) {
        unordered_set<double> unique_scores(scores.begin(), scores.end());
        cout << "Static greedy unique scores: " << unique_scores.size() << endl;
        cout << "Time for computing greedy order: " << greedy_timer << endl;
    }

    assert(order.size() == abstract_state_ids.size());
    return order;
}

class OrderGeneratorGreedyFeature
    : public plugins::TypedFeature<OrderGenerator, OrderGeneratorGreedy> {
public:
    OrderGeneratorGreedyFeature() : TypedFeature("greedy_orders") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Greedy orders");
        document_synopsis("Order abstractions greedily by a given scoring function.");
        add_scoring_function_to_feature(*this);
        add_order_generator_arguments_to_feature(*this);
    }

    virtual shared_ptr<OrderGeneratorGreedy> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<OrderGeneratorGreedy>(
            options.get<ScoringFunction>("scoring_function"),
            get_order_generator_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<OrderGeneratorGreedyFeature> _plugin;
}
