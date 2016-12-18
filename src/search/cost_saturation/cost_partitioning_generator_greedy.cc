#include "cost_partitioning_generator_greedy.h"

#include "abstraction.h"
#include "diversifier.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/collections.h"
#include "../utils/logging.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace cost_saturation {
CostPartitioningGeneratorGreedy::CostPartitioningGeneratorGreedy(const Options &opts)
    : CostPartitioningGenerator(opts),
      increasing_ratios(opts.get<bool>("increasing_ratios")) {
    if (max_orders != 1) {
        cerr << "greedy generator currently needs max_orders = 1" << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
}

static int compute_sum(const vector<int> &vec) {
    int sum = 0;
    for (int val : vec) {
        assert(val != INF);
        if (val == -INF) {
            return -INF;
        } else {
            sum += val;
        }
    }
    return sum;
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

static double compute_h_per_cost_ratio(int h, int cost) {
    assert(h >= 0);
    assert(abs(cost != INF));
    // Make sure we divide two positive numbers.
    int min_cost = 0;
    int shift = min_cost < 0 ? abs(min_cost) : 0;
    return static_cast<double>(h + shift) / (cost + shift);
}

static vector<int> compute_greedy_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    const vector<vector<int>> h_values_by_abstraction,
    const vector<double> used_costs_by_abstraction) {
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
        ratios.push_back(compute_h_per_cost_ratio(h, cost));
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
    vector<vector<int>> orders;
    for (int sample_id = 0; sample_id < num_samples; ++sample_id) {
        const vector<int> &local_state_ids = local_state_ids_by_sample[sample_id];
        orders.push_back(compute_greedy_order_for_sample(
            abstractions, local_state_ids, h_values_by_abstraction, used_costs_by_abstraction));
    }
    cout << "Greedy orders: " << endl;
    for (auto &order : orders) {
        cout << order << endl;
    }
}

CostPartitioning CostPartitioningGeneratorGreedy::get_next_cost_partitioning(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {
    State initial_state = task_proxy.get_initial_state();
    int num_abstractions = abstractions.size();

    set<int> unused_abstractions;
    for (int i = 0; i < num_abstractions; ++i) {
        unused_abstractions.insert(i);
    }
    vector<int> order;

    while (!unused_abstractions.empty()) {
        double max_h_per_costs = -numeric_limits<double>::infinity();
        int min_costs = numeric_limits<int>::max();
        int best_pos = -1;
        for (int i : unused_abstractions) {
            const Abstraction &abstraction = *abstractions[i];
            auto pair = abstraction.compute_goal_distances_and_saturated_costs(costs);
            vector<int> &h_values = pair.first;
            vector<int> &saturated_costs = pair.second;
            int initial_state_id = abstraction.get_abstract_state_id(initial_state);
            double init_h = h_values[initial_state_id];
            int used_costs = compute_sum(saturated_costs);
            double h_per_costs = static_cast<double>(init_h) / max(1, used_costs);
            if (h_per_costs > max_h_per_costs ||
                (h_per_costs == max_h_per_costs && used_costs < min_costs)) {
                best_pos = i;
                max_h_per_costs = h_per_costs;
                min_costs = used_costs;
            }
            cout << i << ": " << " " << init_h << " / " << used_costs
                 << " = " << h_per_costs << endl;
        }
        assert(best_pos != -1);
        order.push_back(best_pos);
        unused_abstractions.erase(best_pos);
        cout << "Use: " << best_pos << endl;
    }
    assert(order.size() == abstractions.size());
    if (increasing_ratios) {
        reverse(order.begin(), order.end());
    }
    cout << "Order: " << order << endl;
    return cp_function(abstractions, order, costs);
}


static shared_ptr<CostPartitioningGenerator> _parse_greedy(OptionParser &parser) {
    parser.add_option<bool>(
        "increasing_ratios",
        "sort by increasing h/costs ratios",
        "false");
    add_common_scp_generator_options_to_parser(parser);
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<CostPartitioningGeneratorGreedy>(opts);
}

static PluginShared<CostPartitioningGenerator> _plugin_greedy(
    "greedy", _parse_greedy);
}
