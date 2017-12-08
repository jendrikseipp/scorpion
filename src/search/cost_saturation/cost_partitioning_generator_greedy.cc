#include "cost_partitioning_generator_greedy.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
CostPartitioningGeneratorGreedy::CostPartitioningGeneratorGreedy(const Options &opts)
    : CostPartitioningGenerator(),
      reverse_initial_order(opts.get<bool>("reverse_initial_order")),
      scoring_function(static_cast<ScoringFunction>(opts.get_enum("scoring_function"))),
      use_negative_costs(opts.get<bool>("use_negative_costs")),
      dynamic(opts.get<bool>("dynamic")),
      rng(utils::parse_rng_from_options(opts)),
      num_returned_orders(0) {
}

static int compute_used_costs(const vector<int> &saturated_costs, bool use_negative_costs) {
    int sum = 0;
    for (int cost : saturated_costs) {
        assert(cost != INF);
        if (cost != -INF && (use_negative_costs || cost > 0)) {
            sum += cost;
        }
    }
    return sum;
}

static double rate_heuristic(
    int h, int used_costs, ScoringFunction scoring_function, bool use_negative_costs) {
    assert(h >= 0);
    assert(used_costs != INF);
    assert(used_costs != -INF);
    if (use_negative_costs && used_costs <= 0) {
        cout << "Used-costs sum is zero or less: " << used_costs << endl;
        used_costs = 0;
    }
    assert(used_costs >= 0);
    if (scoring_function == ScoringFunction::MAX_HEURISTIC) {
        return h;
    } else if (scoring_function == ScoringFunction::MIN_COSTS) {
        return 1 / (used_costs + 1.0);
    } else if (scoring_function == ScoringFunction::MAX_HEURISTIC_PER_COSTS) {
        return h / (used_costs + 1.0);
    } else {
        ABORT("Invalid scoring_function");
    }
}

static vector<int> compute_static_greedy_order_for_sample(
    const vector<int> &local_state_ids,
    const vector<vector<int>> &h_values_by_abstraction,
    const vector<int> &used_costs_by_abstraction,
    ScoringFunction scoring_function,
    bool use_negative_costs) {
    assert(local_state_ids.size() == h_values_by_abstraction.size());
    assert(local_state_ids.size() == used_costs_by_abstraction.size());

    int num_abstractions = local_state_ids.size();
    vector<double> ratios;
    for (int abstraction_id = 0; abstraction_id < num_abstractions; ++abstraction_id) {
        assert(utils::in_bounds(abstraction_id, local_state_ids));
        int local_state_id = local_state_ids[abstraction_id];
        assert(utils::in_bounds(abstraction_id, h_values_by_abstraction));
        assert(utils::in_bounds(local_state_id, h_values_by_abstraction[abstraction_id]));
        int h = h_values_by_abstraction[abstraction_id][local_state_id];
        assert(utils::in_bounds(abstraction_id, used_costs_by_abstraction));
        int used_costs = used_costs_by_abstraction[abstraction_id];
        ratios.push_back(rate_heuristic(h, used_costs, scoring_function, use_negative_costs));
    }

    vector<int> order = get_default_order(num_abstractions);
    sort(order.begin(), order.end(), [&](int abstraction1_id, int abstraction2_id) {
            return ratios[abstraction1_id] > ratios[abstraction2_id];
        });

    return order;
}

vector<int> compute_greedy_dynamic_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    vector<int> remaining_costs,
    ScoringFunction scoring_function,
    bool use_negative_costs) {
    assert(abstractions.size() == local_state_ids.size());
    vector<int> order;

    set<int> remaining_abstractions;
    int num_abstractions = abstractions.size();
    for (int i = 0; i < num_abstractions; ++i) {
        remaining_abstractions.insert(i);
    }

    while (!remaining_abstractions.empty()) {
        double highest_ratio = -numeric_limits<double>::max();
        int best_abstraction = -1;
        vector<int> saturated_costs_for_best_abstraction;
        for (auto it = remaining_abstractions.begin(); it != remaining_abstractions.end();) {
            int abstraction_id = *it;
            assert(utils::in_bounds(abstraction_id, local_state_ids));
            int local_state_id = local_state_ids[abstraction_id];
            Abstraction &abstraction = *abstractions[abstraction_id];
            auto pair = abstraction.compute_goal_distances_and_saturated_costs(remaining_costs);
            vector<int> &h_values = pair.first;
            vector<int> &saturated_costs = pair.second;
            assert(utils::in_bounds(local_state_id, h_values));
            int h = h_values[local_state_id];
            int used_costs = compute_used_costs(saturated_costs, use_negative_costs);
            double ratio = rate_heuristic(h, used_costs, scoring_function, use_negative_costs);
            if (ratio > highest_ratio) {
                best_abstraction = abstraction_id;
                saturated_costs_for_best_abstraction = move(saturated_costs);
                highest_ratio = ratio;
                ++it;
            } else {
                ++it;
            }
        }
        if (best_abstraction != -1) {
            order.push_back(best_abstraction);
            remaining_abstractions.erase(best_abstraction);
            reduce_costs(remaining_costs, saturated_costs_for_best_abstraction);
        }
    }

    assert(order.size() == abstractions.size());
    return order;
}

void CostPartitioningGeneratorGreedy::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    random_order = get_default_order(abstractions.size());

    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        auto pair = abstraction->compute_goal_distances_and_saturated_costs(costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        h_values_by_abstraction.push_back(move(h_values));
        int used_costs = compute_used_costs(saturated_costs, use_negative_costs);
        used_costs_by_abstraction.push_back(used_costs);
    }
}

Order CostPartitioningGeneratorGreedy::get_next_order(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    const vector<int> &local_state_ids) {
    // We can call compute_sum_h with unpartitioned h values since we only need
    // a safe, but not necessarily admissible estimate.
    assert(compute_sum_h(local_state_ids, h_values_by_abstraction) != INF);

    // Only be verbose for first sample.
    bool verbose = (num_returned_orders == 0);

    utils::Timer greedy_timer;
    vector<int> order;
    if (scoring_function == ScoringFunction::RANDOM) {
        rng->shuffle(random_order);
        order = random_order;
    } else if (dynamic) {
        order = compute_greedy_dynamic_order_for_sample(
            abstractions, local_state_ids, costs, scoring_function, use_negative_costs);
    } else {
        order = compute_static_greedy_order_for_sample(
            local_state_ids, h_values_by_abstraction, used_costs_by_abstraction,
            scoring_function, use_negative_costs);
    }

    if (reverse_initial_order) {
        reverse(order.begin(), order.end());
    }

    if (verbose) {
        utils::Log() << "Time for computing greedy order: " << greedy_timer << endl;
    }

    ++num_returned_orders;
    return order;
}


void add_scoring_function_to_parser(OptionParser &parser) {
    vector<string> scoring_functions;
    scoring_functions.push_back("RANDOM");
    scoring_functions.push_back("MAX_HEURISTIC");
    scoring_functions.push_back("MIN_COSTS");
    scoring_functions.push_back("MAX_HEURISTIC_PER_COSTS");
    parser.add_enum_option(
        "scoring_function", scoring_functions, "scoring function", "MAX_HEURISTIC_PER_COSTS");
}

static shared_ptr<CostPartitioningGenerator> _parse_greedy(OptionParser &parser) {
    parser.add_option<bool>(
        "reverse_initial_order",
        "invert initial order",
        "false");
    add_scoring_function_to_parser(parser);
    parser.add_option<bool>(
        "use_negative_costs",
        "account for negative costs when computing used costs",
        "false");
    parser.add_option<bool>(
        "dynamic",
        "recompute ratios in each step",
        "false");
    utils::add_rng_options(parser);
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<CostPartitioningGeneratorGreedy>(opts);
}

static PluginShared<CostPartitioningGenerator> _plugin_greedy(
    "greedy", _parse_greedy);
}
