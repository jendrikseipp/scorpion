#include "cost_partitioning_generator_greedy.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
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
      queue_zero_ratios(opts.get<bool>("queue_zero_ratios")),
      dynamic(opts.get<bool>("dynamic")),
      steepest_ascent(opts.get<bool>("steepest_ascent")),
      max_optimization_time(opts.get<double>("max_optimization_time")),
      filter_blind_heuristics(opts.get<bool>("filter_blind_heuristics")),
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
    vector<int> h_values;
    vector<double> ratios;
    for (int abstraction_id = 0; abstraction_id < num_abstractions; ++abstraction_id) {
        assert(utils::in_bounds(abstraction_id, local_state_ids));
        int local_state_id = local_state_ids[abstraction_id];
        assert(utils::in_bounds(abstraction_id, h_values_by_abstraction));
        assert(utils::in_bounds(local_state_id, h_values_by_abstraction[abstraction_id]));
        int h = h_values_by_abstraction[abstraction_id][local_state_id];
        h_values.push_back(h);
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
    bool queue_zero_ratios,
    ScoringFunction scoring_function,
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
            double ratio = rate_heuristic(h, used_costs, scoring_function, use_negative_costs);
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
    utils::Log() << "Switch positions " << i << " and " << j << " (abstractions "
                 << order[j] << ", " << order[i] << "): h=" << h << endl;
    utils::Log() << "Found improving order with h=" << h << ": " << order << endl;
}

static bool search_improving_successor(
    CPFunction cp_function,
    const utils::CountdownTimer &timer,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    const vector<int> &local_state_ids,
    vector<int> &incumbent_order,
    CostPartitionedHeuristic &incumbent_cp,
    int &incumbent_h_value,
    bool steepest_ascent,
    bool filter_blind_heuristics,
    bool verbose) {
    int num_abstractions = abstractions.size();
    int best_i = -1;
    int best_j = -1;
    for (int i = 0; i < num_abstractions && !timer.is_expired(); ++i) {
        for (int j = i + 1; j < num_abstractions && !timer.is_expired(); ++j) {
            swap(incumbent_order[i], incumbent_order[j]);

            CostPartitionedHeuristic neighbor_cp =
                cp_function(abstractions, incumbent_order, costs, filter_blind_heuristics);

            int h = neighbor_cp.compute_heuristic(local_state_ids);
            if (h > incumbent_h_value) {
                incumbent_cp = move(neighbor_cp);
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
                    return true;
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
    CostPartitionedHeuristic &incumbent_cp,
    int incumbent_h_value,
    bool steepest_ascent,
    bool filter_blind_heuristics,
    bool verbose) {
    if (verbose) {
        utils::Log() << "Incumbent h value: " << incumbent_h_value << endl;
    }
    while (!timer.is_expired()) {
        bool success = search_improving_successor(
            cp_function, timer, abstractions, costs, local_state_ids,
            incumbent_order, incumbent_cp, incumbent_h_value, steepest_ascent,
            filter_blind_heuristics, verbose);
        if (!success) {
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
        int used_costs = compute_used_costs(saturated_costs, use_negative_costs);
        used_costs_by_abstraction.push_back(used_costs);
    }
}

CostPartitionedHeuristic CostPartitioningGeneratorGreedy::get_next_cost_partitioning(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    const State &state,
    CPFunction cp_function) {
    vector<int> local_state_ids = get_local_state_ids(abstractions, state);

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
            abstractions, local_state_ids, costs,
            queue_zero_ratios, scoring_function, use_negative_costs);
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
    if (max_optimization_time > 0) {
        utils::CountdownTimer timer(max_optimization_time);
        CostPartitionedHeuristic incumbent_cp = cp_function(abstractions, order, costs, filter_blind_heuristics);
        int incumbent_h_value = incumbent_cp.compute_heuristic(local_state_ids);
        do_hill_climbing(
            cp_function, timer, abstractions, costs, local_state_ids, order,
            incumbent_cp, incumbent_h_value, steepest_ascent, filter_blind_heuristics, verbose);
        if (verbose) {
            utils::Log() << "Time for optimizing order: " << timer << endl;
            utils::Log() << "Time for optimizing order has expired: " << timer.is_expired() << endl;
        }
        return incumbent_cp;
    } else {
        return cp_function(abstractions, order, costs, filter_blind_heuristics);
    }
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
        "queue_zero_ratios",
        "put abstraction with ratio=0 to the end of the order",
        "true");
    parser.add_option<bool>(
        "dynamic",
        "recompute ratios in each step",
        "false");
    parser.add_option<bool>(
        "steepest_ascent",
        "do steepest-ascent hill climbing instead of selecting the first improving successor",
        "false");
    parser.add_option<double>(
        "max_optimization_time",
        "maximum time for optimizing",
        "0.0",
        Bounds("0.0", "infinity"));
    parser.add_option<bool>(
        "filter_blind_heuristics",
        "don't store h-value vectors that only contain zeros",
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
