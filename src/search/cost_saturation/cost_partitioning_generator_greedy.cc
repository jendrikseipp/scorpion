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
      use_random_initial_order(opts.get<bool>("use_random_initial_order")),
      increasing_ratios(opts.get<bool>("increasing_ratios")),
      use_stolen_costs(opts.get<bool>("use_stolen_costs")),
      use_negative_costs(opts.get<bool>("use_negative_costs")),
      queue_zero_ratios(opts.get<bool>("queue_zero_ratios")),
      dynamic(opts.get<bool>("dynamic")),
      steepest_ascent(opts.get<bool>("steepest_ascent")),
      continue_after_switch(opts.get<bool>("continue_after_switch")),
      max_optimization_time(opts.get<double>("max_optimization_time")),
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

static int bounded_addition(int a, int b) {
    int inf = numeric_limits<int>::max();
    if ((a == inf && b == -inf) || (a == -inf && b == inf)) {
        ABORT("Can't sum positive and negative infinity.");
    }
    if (a == inf || b == inf) {
        return inf;
    } else if (a == -inf || b == -inf) {
        return -inf;
    }
    int result = a + b;
    if (a >= 0 && b >= 0 && result < 0) {
        return inf;
    } else if (a <= 0 && b <= 0 && result > 0) {
        return -inf;
    }
    return result;
 }

/*
  Compute the amount of costs that an abstraction A wants to "steal" from or
  "provide" to other abstractions ("negative stealing").

  if ĉ_A(o) >= 0:
      stolen_A(o) = max(0, ĉ_A(o) - max(0, c(o) - \sum_{abstractions B} ĉ_B(o)))
  else:
      stolen_A(o) = max(ĉ_A(o), min(0, c(o) - \sum_{abstractions B} ĉ_B(o)))
*/
static int compute_stolen_costs(
    const vector<int> &original_costs,
    const vector<int> &saturated_costs,
    const vector<int> &total_saturated_costs,
    bool use_negative_costs) {
    assert(original_costs.size() == saturated_costs.size());
    assert(saturated_costs.size() == total_saturated_costs.size());
    bool debug = false;
    int sum = 0;
    int num_operators = original_costs.size();
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        int wanted_costs = saturated_costs[op_id];
        assert(wanted_costs != INF);
        int costs_wanted_by_others = (total_saturated_costs[op_id] == -INF) ?
            -INF : bounded_addition(total_saturated_costs[op_id], -wanted_costs);
        int costs_nobody_else_wants = bounded_addition(
            original_costs[op_id], -costs_wanted_by_others);
        int stolen_cost = 0;
        if (wanted_costs >= 0) {
            stolen_cost = max(0, bounded_addition(
                wanted_costs, -max(0, costs_nobody_else_wants)));
        } else if (use_negative_costs) {
            assert(wanted_costs < 0);
            stolen_cost = max(wanted_costs, min(0, costs_nobody_else_wants));
        }
        assert(stolen_cost != INF);
        assert(stolen_cost != -INF);
        sum += stolen_cost;
        if (debug) {
            cout << "wanted: " << wanted_costs << ", original: "
                 << original_costs[op_id] << ", others: "
                 << costs_wanted_by_others << ", result: " << stolen_cost << endl;
        }
    }
    return sum;
}

static double compute_h_per_cost_ratio(int h, int used_costs, bool use_negative_costs) {
    assert(h >= 0);
    assert(used_costs != INF);
    assert(used_costs != -INF);
    if (use_negative_costs && used_costs <= 0) {
        cout << "Used-costs sum is zero or less: " << used_costs << endl;
        used_costs = 0;
    }
    assert(used_costs >= 0);
    return h / (used_costs + 1.0);
}

static vector<int> compute_greedy_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    const vector<vector<int>> h_values_by_abstraction,
    const vector<double> used_costs_by_abstraction,
    bool use_negative_costs,
    bool verbose) {
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
        ratios.push_back(compute_h_per_cost_ratio(h, used_costs, use_negative_costs));
    }

    if (verbose) {
        cout << "h-values: ";
        print_indexed_vector(h_values);
        cout << "Ratios: ";
        print_indexed_vector(ratios);
    }

    vector<int> order = get_default_order(abstractions.size());
    sort(order.begin(), order.end(), [&](int abstraction1_id, int abstraction2_id) {
        return ratios[abstraction1_id] > ratios[abstraction2_id];
    });

    return order;
}

vector<int> compute_greedy_dynamic_order_for_sample(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &local_state_ids,
    vector<int> remaining_costs,
    bool queue_zero_ratios,
    bool use_negative_costs) {
    assert(abstractions.size() == local_state_ids.size());

    vector<int> order;
    vector<int> abstractions_with_zero_h;

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
            double ratio = compute_h_per_cost_ratio(h, used_costs, use_negative_costs);
            if (queue_zero_ratios && h == 0) {
                abstractions_with_zero_h.push_back(abstraction_id);
                it = remaining_abstractions.erase(it);
            } else if (ratio > highest_ratio) {
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

    order.insert(order.end(), abstractions_with_zero_h.begin(), abstractions_with_zero_h.end());
    assert(order.size() == abstractions.size());
    return order;
}

static void log_better_order(const vector<int> &order, int h, int i, int j) {
    utils::Log() << "Switch positions " << i << " and " << j << ": h=" << h << endl;
    utils::Log() << "Found improving order with h=" << h << ": " << order << endl;
}

static bool search_improving_successor(
    CPFunction cp_function,
    const utils::CountdownTimer &timer,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    const vector<int> &local_state_ids,
    vector<int> &incumbent_order,
    int &incumbent_h_value,
    bool steepest_ascent,
    bool continue_after_switch,
    bool verbose) {
    utils::unused_variable(steepest_ascent);
    int num_abstractions = abstractions.size();
    int best_i = -1;
    int best_j = -1;
    for (int i = 0; i < num_abstractions && !timer.is_expired(); ++i) {
        if (verbose) {
            utils::Log() << "Check position " << i << endl;
        }
        for (int j = i + 1; j < num_abstractions && !timer.is_expired(); ++j) {
            swap(incumbent_order[i], incumbent_order[j]);

            vector<vector<int>> h_values_by_abstraction =
                cp_function(abstractions, incumbent_order, costs);

            int h = compute_sum_h(local_state_ids, h_values_by_abstraction);
            if (h > incumbent_h_value) {
                incumbent_h_value = h;
                best_i = i;
                best_j = j;
                if (steepest_ascent) {
                    // Restore incumbent order.
                    swap(incumbent_order[i], incumbent_order[j]);
                } else {
                    if (verbose) {
                        log_better_order(incumbent_order, h, i, j);
                    }
                    if (!continue_after_switch) {
                        return true;
                    }
                }

            } else {
                // Restore incumbent order.
                swap(incumbent_order[i], incumbent_order[j]);
            }
        }
    }
    if (best_i != -1) {
        assert(best_j != -1);
        if (steepest_ascent) {
            swap(incumbent_order[best_i], incumbent_order[best_j]);
            if (verbose) {
                log_better_order(incumbent_order, incumbent_h_value, best_i, best_j);
            }
        }
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
    bool steepest_ascent,
    bool continue_after_switch,
    bool verbose) {
    vector<vector<int>> h_values_by_abstraction = cp_function(
        abstractions, incumbent_order, costs);
    int incumbent_h_value = compute_sum_h(local_state_ids, h_values_by_abstraction);
    if (verbose) {
        utils::Log() << "Incumbent h value: " << incumbent_h_value << endl;
    }
    while (!timer.is_expired()) {
        bool success = search_improving_successor(
            cp_function, timer, abstractions, costs, local_state_ids,
            incumbent_order, incumbent_h_value, steepest_ascent,
            continue_after_switch, verbose);
        if (!success) {
            break;
        }
    }
}

void CostPartitioningGeneratorGreedy::initialize(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs) {
    successor_generator = utils::make_unique_ptr<SuccessorGenerator>(task_proxy);
    average_operator_costs = get_average_operator_cost(task_proxy);
    initial_state = utils::make_unique_ptr<State>(task_proxy.get_initial_state());

    vector<int> default_order = get_default_order(abstractions.size());
    CostPartitioning scp_for_default_order =
        compute_saturated_cost_partitioning(abstractions, default_order, costs);

    std::function<int (const State &state)> default_order_heuristic =
        [&abstractions, &scp_for_default_order](const State &state) {
            vector<int> local_state_ids = get_local_state_ids(abstractions, state);
            return compute_sum_h(local_state_ids, scp_for_default_order);
        };

    init_h = default_order_heuristic(*initial_state);
    cout << "Initial h value for default order: " << init_h << endl;

    random_order = default_order;

    if (!use_stolen_costs) {
        for (const unique_ptr<Abstraction> &abstraction : abstractions) {
            auto pair = abstraction->compute_goal_distances_and_saturated_costs(costs);
            vector<int> &h_values = pair.first;
            vector<int> &saturated_costs = pair.second;
            h_values_by_abstraction.push_back(move(h_values));
            int used_costs = compute_used_costs(saturated_costs, use_negative_costs);
            used_costs_by_abstraction.push_back(used_costs);
        }
    } else {
        vector<vector<int>> saturated_costs_by_abstraction;
        for (const unique_ptr<Abstraction> &abstraction : abstractions) {
            auto pair = abstraction->compute_goal_distances_and_saturated_costs(costs);
            vector<int> &h_values = pair.first;
            vector<int> &saturated_costs = pair.second;
            h_values_by_abstraction.push_back(move(h_values));
            saturated_costs_by_abstraction.push_back(move(saturated_costs));
        }
        int num_operators = task_proxy.get_operators().size();
        vector<int> total_saturated_costs_by_operator(num_operators, 0);
        for (const vector<int> &saturated_costs : saturated_costs_by_abstraction) {
            assert(static_cast<int>(saturated_costs.size()) == num_operators);
            for (int op_id = 0; op_id < num_operators; ++op_id) {
                int saturated = saturated_costs[op_id];
                if (use_negative_costs || saturated >= 0) {
                    assert(saturated != INF);
                    total_saturated_costs_by_operator[op_id] = bounded_addition(
                        saturated, total_saturated_costs_by_operator[op_id]);
                }
            }
        }
        for (size_t i = 0; i < abstractions.size(); ++i) {
            int stolen_costs = compute_stolen_costs(
                costs,
                saturated_costs_by_abstraction[i],
                total_saturated_costs_by_operator,
                use_negative_costs);
            used_costs_by_abstraction.push_back(stolen_costs);
        }
    }
    cout << "Used costs by abstraction: ";
    print_indexed_vector(used_costs_by_abstraction);
}

CostPartitioning CostPartitioningGeneratorGreedy::get_next_cost_partitioning(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function) {

    // Always start with the initial state before turning to samples.
    State sample = *initial_state;
    if (num_returned_orders != 0) {
        sample = sample_state_with_random_walk(
            *initial_state,
            *successor_generator,
            init_h,
            average_operator_costs,
            *rng);
    }

    vector<int> local_state_ids = get_local_state_ids(abstractions, sample);

    // We can call compute_sum_h with unpartitioned h values since we only need
    // a safe, but not necessarily admissible estimate.
    if (compute_sum_h(local_state_ids, h_values_by_abstraction) == INF) {
        rng->shuffle(random_order);
        return cp_function(abstractions, random_order, costs);
    }

    // Only be verbose for first sample.
    bool verbose = (num_returned_orders == 0);

    utils::Timer greedy_timer;
    vector<int> order;
    if (use_random_initial_order) {
        rng->shuffle(random_order);
        order = random_order;
    } else if (dynamic) {
        order = compute_greedy_dynamic_order_for_sample(
            abstractions, local_state_ids, costs, queue_zero_ratios, use_negative_costs);
    } else {
        order = compute_greedy_order_for_sample(
        abstractions, local_state_ids, h_values_by_abstraction,
        used_costs_by_abstraction, use_negative_costs, verbose);
    }

    if (increasing_ratios) {
        reverse(order.begin(), order.end());
    }

    if (verbose) {
        cout << "Time for computing greedy order: " << greedy_timer << endl;
        utils::Log() << "Greedy order: " << order << endl;
    }

    if (max_optimization_time > 0) {
        utils::CountdownTimer timer(max_optimization_time);
        do_hill_climbing(
            cp_function, timer, abstractions, costs, local_state_ids, order,
            steepest_ascent, continue_after_switch, verbose);
        if (verbose) {
            cout << "Time for optimizing order: " << timer << endl;
            cout << "Time for optimizing order has expired: " << timer.is_expired() << endl;
            cout << "Optimized order: " << order << endl;
        }
    }

    ++num_returned_orders;
    return cp_function(abstractions, order, costs);
}


static shared_ptr<CostPartitioningGenerator> _parse_greedy(OptionParser &parser) {
    parser.add_option<bool>(
        "use_random_initial_order",
        "use random instead of greedy order",
        "false");
    parser.add_option<bool>(
        "increasing_ratios",
        "sort by increasing h/costs ratios",
        "false");
    parser.add_option<bool>(
        "use_stolen_costs",
        "define used costs as costs taken by an abstraction that could have been"
        " used by other abstractions",
        "false");
    parser.add_option<bool>(
        "use_negative_costs",
        "account for negative costs when computing used/stolen costs",
        "false");
    parser.add_option<bool>(
        "queue_zero_ratios",
        "put abstraction with ratio=0 to the end of the order",
        "false");
    parser.add_option<bool>(
        "dynamic",
        "recompute ratios in each step",
        "false");
    parser.add_option<bool>(
        "steepest_ascent",
        "do steepest-ascent hill climbing instead of selecting the first improving successor",
        "false");
    parser.add_option<bool>(
        "continue_after_switch",
        "after switching heuristics i and j, check (i, j+1) or (i+1, i+2) instead of (0, 1)",
        "false");
    parser.add_option<double>(
        "max_optimization_time",
        "maximum time for optimizing",
        "0.0",
        Bounds("0.0", "infinity"));
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
