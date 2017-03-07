#include "cost_partitioning_generator_greedy.h"

#include "abstraction.h"
#include "diversifier.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_proxy.h"
#include "../task_tools.h"

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
    : CostPartitioningGenerator(opts),
      increasing_ratios(opts.get<bool>("increasing_ratios")),
      dynamic(opts.get<bool>("dynamic")),
      optimize(opts.get<bool>("optimize")),
      steepest_ascent(opts.get<bool>("steepest_ascent")),
      rng(utils::parse_rng_from_options(opts)),
      min_used_costs(numeric_limits<int>::max()) {
}

static int sum_positive_values(const vector<int> &vec) {
    int sum = 0;
    for (int val : vec) {
        assert(val != INF);
        if (val > 0) {
            sum += val;
        }
    }
    return sum;
}

static double compute_h_per_cost_ratio(int h, int used_costs) {
    assert(h >= 0);
    assert(used_costs >= 0 && used_costs != INF);
    assert(h <= used_costs);
    return used_costs == 0 ? 0 : h / static_cast<double>(used_costs);
}

static vector<int> compute_greedy_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    const vector<vector<int>> h_values_by_abstraction,
    const vector<double> used_costs_by_abstraction,
    int min_used_costs) {
    utils::unused_variable(min_used_costs);
    assert(abstractions.size() == local_state_ids.size());
    assert(abstractions.size() == h_values_by_abstraction.size());
    assert(abstractions.size() == used_costs_by_abstraction.size());

    vector<int> h_values;
    vector<double> ratios;
    for (size_t abstraction_id = 0; abstraction_id < abstractions.size(); ++abstraction_id) {
        assert(utils::in_bounds(abstraction_id, local_state_ids));
        int local_state_id = local_state_ids[abstraction_id];
        assert(utils::in_bounds(abstraction_id, h_values_by_abstraction));
        assert(utils::in_bounds(local_state_id, h_values_by_abstraction[abstraction_id]));
        int h = h_values_by_abstraction[abstraction_id][local_state_id];
        h_values.push_back(h);
        assert(utils::in_bounds(abstraction_id, used_costs_by_abstraction));
        int used_costs = used_costs_by_abstraction[abstraction_id];
        ratios.push_back(compute_h_per_cost_ratio(h, used_costs));
    }

    cout << "h-values: ";
    print_indexed_vector(h_values);
    cout << "Ratios: ";
    print_indexed_vector(ratios);

    vector<int> order = get_default_order(abstractions.size());
    sort(order.begin(), order.end(), [&](int abstraction1_id, int abstraction2_id) {
        return ratios[abstraction1_id] > ratios[abstraction2_id];
    });

    return order;
}

vector<int> compute_greedy_dynamic_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    vector<int> remaining_costs) {
    // TODO (as for static version): Only consider operators that are active in
    // other abstractions when computing used costs.
    // TODO: We can put an abstraction to the end of the order once its ratios is 0.
    // TODO: Use gained costs as tiebreaker.
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
        for (int abstraction_id : remaining_abstractions) {
            assert(utils::in_bounds(abstraction_id, local_state_ids));
            int local_state_id = local_state_ids[abstraction_id];
            Abstraction &abstraction = *abstractions[abstraction_id];
            auto pair = abstraction.compute_goal_distances_and_saturated_costs(remaining_costs);
            vector<int> &h_values = pair.first;
            vector<int> &saturated_costs = pair.second;
            assert(utils::in_bounds(local_state_id, h_values));
            int h = h_values[local_state_id];
            h_values.push_back(h);
            int used_costs = sum_positive_values(saturated_costs);
            double ratio = compute_h_per_cost_ratio(h, used_costs);
            if (ratio > highest_ratio) {
                best_abstraction = abstraction_id;
                saturated_costs_for_best_abstraction = move(saturated_costs);
                highest_ratio = ratio;
            }
        }
        assert(best_abstraction != -1);
        order.push_back(best_abstraction);
        remaining_abstractions.erase(best_abstraction);
        reduce_costs(remaining_costs, saturated_costs_for_best_abstraction);
    }

    return order;
}

static bool search_improving_successor(
    CPFunction cp_function,
    const utils::CountdownTimer &timer,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    const vector<int> &local_state_ids,
    vector<int> &incumbent_order,
    int &incumbent_h_value,
    bool steepest_ascent) {
    utils::unused_variable(steepest_ascent);
    int num_abstractions = abstractions.size();
    int best_i = -1;
    int best_j = -1;
    for (int i = 0; i < num_abstractions && !timer.is_expired(); ++i) {
        for (int j = i + 1; j < num_abstractions && !timer.is_expired(); ++j) {
            swap(incumbent_order[i], incumbent_order[j]);

            vector<vector<int>> h_values_by_abstraction =
                cp_function(abstractions, incumbent_order, costs);

            int h = compute_sum_h(local_state_ids, h_values_by_abstraction);
            if (h > incumbent_h_value) {
                incumbent_h_value = h;
                if (!steepest_ascent) {
                    return true;
                }
                best_i = i;
                best_j = j;
            }
            // Restore incumbent order.
            swap(incumbent_order[i], incumbent_order[j]);
        }
    }
    if (best_i != -1) {
        assert(best_j != -1);
        swap(incumbent_order[best_i], incumbent_order[best_j]);
        return true;
    }
    return false;
}


static void do_hill_climbing(
    CPFunction cp_function,
    const utils::CountdownTimer &timer,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    const vector<int> &local_state_ids,
    vector<int> &incumbent_order,
    bool steepest_ascent) {
    vector<vector<int>> h_values_by_abstraction = cp_function(
        abstractions, incumbent_order, costs);
    int incumbent_h_value = compute_sum_h(local_state_ids, h_values_by_abstraction);
    while (!timer.is_expired()) {
        bool success = search_improving_successor(
            cp_function, timer, abstractions, costs, local_state_ids,
            incumbent_order, incumbent_h_value, steepest_ascent);
        if (success) {
            cout << "Found improving order with h=" << incumbent_h_value
                 << ": " << incumbent_order << endl;
        } else {
            break;
        }
    }
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
        int used_costs = sum_positive_values(saturated_costs);
        used_costs_by_abstraction.push_back(used_costs);
        min_used_costs = min(min_used_costs, used_costs);
    }
    cout << "Used costs by abstraction: ";
    print_indexed_vector(used_costs_by_abstraction);
    cout << "Minimum used costs: " << min_used_costs << endl;
}

CostPartitioning CostPartitioningGeneratorGreedy::get_next_cost_partitioning(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    State sample = task_proxy.get_initial_state();

    vector<int> local_state_ids = get_local_state_ids(abstractions, sample);

    // We can call compute_sum_h with unpartitioned h values since we only need
    // a safe, but not necessarily admissible estimate.
    if (compute_sum_h(local_state_ids, h_values_by_abstraction) == INF) {
        rng->shuffle(random_order);
        return cp_function(abstractions, random_order, costs);
    }

    vector<int> order;
    if (dynamic) {
        order = compute_greedy_dynamic_order_for_sample(
            abstractions, local_state_ids, costs);
    } else {
        order = compute_greedy_order_for_sample(
        abstractions, local_state_ids, h_values_by_abstraction,
        used_costs_by_abstraction, min_used_costs);
    }

    if (increasing_ratios) {
        reverse(order.begin(), order.end());
    }

    cout << "Greedy order: " << order << endl;

    if (optimize) {
        utils::CountdownTimer timer(numeric_limits<double>::max());
        do_hill_climbing(
            cp_function, timer, abstractions, costs, local_state_ids, order,
            steepest_ascent);
    }

    return cp_function(abstractions, order, costs);
}


static shared_ptr<CostPartitioningGenerator> _parse_greedy(OptionParser &parser) {
    parser.add_option<bool>(
        "increasing_ratios",
        "sort by increasing h/costs ratios",
        "false");
    parser.add_option<bool>(
        "dynamic",
        "recompute ratios in each step",
        "false");
    parser.add_option<bool>(
        "optimize",
        "do a hill climbing search in the space of orders",
        "false");
    parser.add_option<bool>(
        "steepest_ascent",
        "do steepest-ascent hill climbing instead of selecting the first improving successor",
        "false");
    add_common_scp_generator_options_to_parser(parser);
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
