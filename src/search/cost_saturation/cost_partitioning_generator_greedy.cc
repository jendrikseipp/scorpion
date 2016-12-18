#include "cost_partitioning_generator_greedy.h"

#include "abstraction.h"
#include "diversifier.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace cost_saturation {
CostPartitioningGeneratorGreedy::CostPartitioningGeneratorGreedy(const Options &opts)
    : CostPartitioningGenerator(opts),
      increasing_ratios(opts.get<bool>("increasing_ratios")),
      rng(utils::parse_rng_from_options(opts)) {
}

static int compute_finite_sum(const vector<int> &vec) {
    int sum = 0;
    for (int val : vec) {
        assert(val != INF);
        if (val != -INF) {
            sum += val;
        }
    }
    return sum;
}

static double compute_h_per_cost_ratio(int h, int used_costs, int min_used_costs) {
    assert(h >= 0);
    assert(abs(used_costs != INF));
    // Make sure we divide two positive numbers.
    double shift = min_used_costs <= 0 ? abs(min_used_costs) + 1 : 0;
    return (h + shift) / (used_costs + shift);
}

static vector<int> compute_greedy_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    const vector<vector<int>> h_values_by_abstraction,
    const vector<double> used_costs_by_abstraction,
    int min_used_costs) {
    assert(abstractions.size() == local_state_ids.size());
    assert(abstractions.size() == h_values_by_abstraction.size());
    assert(abstractions.size() == used_costs_by_abstraction.size());

    vector<int> order = get_default_order(abstractions.size());

    vector<double> ratios;
    for (size_t abstraction_id = 0; abstraction_id < abstractions.size(); ++abstraction_id) {
        assert(utils::in_bounds(abstraction_id, local_state_ids));
        int local_state_id = local_state_ids[abstraction_id];
        assert(utils::in_bounds(abstraction_id, h_values_by_abstraction));
        assert(utils::in_bounds(local_state_id, h_values_by_abstraction[abstraction_id]));
        int h = h_values_by_abstraction[abstraction_id][local_state_id];
        assert(utils::in_bounds(abstraction_id, used_costs_by_abstraction));
        int cost = used_costs_by_abstraction[abstraction_id];
        ratios.push_back(compute_h_per_cost_ratio(h, cost, min_used_costs));
    }

    cout << "Ratios: " << ratios << endl;

    sort(order.begin(), order.end(), [&](int abstraction1_id, int abstraction2_id) {
        return ratios[abstraction1_id] > ratios[abstraction2_id];
    });

    return order;
}

void CostPartitioningGeneratorGreedy::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    vector<vector<int>> h_values_by_abstraction;
    vector<double> used_costs_by_abstraction;
    int min_used_costs = numeric_limits<int>::max();
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        auto pair = abstraction->compute_goal_distances_and_saturated_costs(costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        h_values_by_abstraction.push_back(move(h_values));
        int used_costs = compute_finite_sum(saturated_costs);
        used_costs_by_abstraction.push_back(used_costs);
        min_used_costs = min(min_used_costs, used_costs);
    }
    cout << "Used costs by abstraction: " << used_costs_by_abstraction << endl;
    if (!diversifier) {
        ABORT("Greedy generator needs diversify=true");
    }
    const vector<vector<int>> &local_state_ids_by_sample =
        diversifier->get_local_state_ids_by_sample();
    int num_samples = local_state_ids_by_sample.size();
    for (int sample_id = 0; sample_id < num_samples; ++sample_id) {
        const vector<int> &local_state_ids = local_state_ids_by_sample[sample_id];
        greedy_orders.push_back(compute_greedy_order_for_sample(
            abstractions, local_state_ids, h_values_by_abstraction,
            used_costs_by_abstraction, min_used_costs));
    }

    random_order = get_default_order(abstractions.size());
}

CostPartitioning CostPartitioningGeneratorGreedy::get_next_cost_partitioning(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    if (greedy_orders.empty()) {
        rng->shuffle(random_order);
        return cp_function(abstractions, random_order, costs);
    } else {
        vector<int> order = move(greedy_orders.back());
        greedy_orders.pop_back();
        if (increasing_ratios) {
            reverse(order.begin(), order.end());
        }
        return cp_function(abstractions, order, costs);
    }
}


static shared_ptr<CostPartitioningGenerator> _parse_greedy(OptionParser &parser) {
    parser.add_option<bool>(
        "increasing_ratios",
        "sort by increasing h/costs ratios",
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
