#include "utils.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
#include "cost_partitioning_generator_greedy.h"

#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_proxy.h"
#include "../task_tools.h"

#include "../options/options.h"
#include "../utils/collections.h"
#include "../utils/timer.h"

#include <cassert>
#include <numeric>

using namespace std;

namespace cost_saturation {
vector<int> get_default_order(int num_abstractions) {
    vector<int> indices(num_abstractions);
    iota(indices.begin(), indices.end(), 0);
    return indices;
}

int compute_sum_h(
    const vector<int> &local_state_ids,
    const vector<vector<int>> &h_values_by_abstraction) {
    int sum_h = 0;
    assert(local_state_ids.size() == h_values_by_abstraction.size());
    for (size_t i = 0; i < local_state_ids.size(); ++i) {
        const vector<int> &h_values = h_values_by_abstraction[i];
        int state_id = local_state_ids[i];
        if (state_id == -1) {
            // Abstract state has been pruned.
            return INF;
        }
        assert(utils::in_bounds(state_id, h_values));
        int value = h_values[state_id];
        assert(value >= 0);
        if (value == INF)
            return INF;
        sum_h += value;
        assert(sum_h >= 0);
    }
    return sum_h;
}

vector<int> get_local_state_ids(
    const Abstractions &abstractions, const State &state) {
    vector<int> local_state_ids;
    local_state_ids.reserve(abstractions.size());
    for (auto &abstraction : abstractions) {
        local_state_ids.push_back(abstraction->get_abstract_state_id(state));
    }
    return local_state_ids;
}

CostPartitionedHeuristic compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool filter_blind_heuristics) {
    const bool debug = false;
    assert(abstractions.size() == order.size());
    CostPartitionedHeuristic cp_heuristic;
    vector<int> remaining_costs = costs;
    for (int pos : order) {
        const Abstraction &abstraction = *abstractions[pos];
        auto pair = abstraction.compute_goal_distances_and_saturated_costs(
            remaining_costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        if (debug) {
            cout << "h-values: ";
            print_indexed_vector(h_values);
            cout << "saturated costs: ";
            print_indexed_vector(saturated_costs);
        }
        cp_heuristic.add_cp_heuristic_values(pos, move(h_values), filter_blind_heuristics);
        reduce_costs(remaining_costs, saturated_costs);
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return cp_heuristic;
}

CostPartitionedHeuristic compute_cost_partitioning_for_static_order(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs,
    CPFunction cp_function,
    const State &state) {
    options::Options greedy_opts;
    greedy_opts.set("reverse_initial_order", false);
    greedy_opts.set("scoring_function", static_cast<int>(ScoringFunction::MAX_HEURISTIC_PER_COSTS));
    greedy_opts.set("use_negative_costs", false);
    greedy_opts.set("queue_zero_ratios", true);
    greedy_opts.set("dynamic", false);
    greedy_opts.set("steepest_ascent", false);
    greedy_opts.set("max_optimization_time", 0.0);
    greedy_opts.set("filter_blind_heuristics", true);
    greedy_opts.set("random_seed", 0);
    CostPartitioningGeneratorGreedy greedy_generator(greedy_opts);
    greedy_generator.initialize(task_proxy, abstractions, costs);
    return greedy_generator.get_next_cost_partitioning(
        task_proxy, abstractions, costs, state, cp_function);
}

vector<State> sample_states(
    const TaskProxy &task_proxy,
    const function<int (const State &state)> &heuristic,
    int num_samples,
    const shared_ptr<utils::RandomNumberGenerator> &rng) {
    assert(num_samples >= 1);
    utils::Timer sampling_timer;
    cout << "Start sampling" << endl;
    State initial_state = task_proxy.get_initial_state();
    int init_h = heuristic(initial_state);
    cout << "Initial h value for sampling: " << init_h << endl;
    if (init_h == INF) {
        return {
                   move(initial_state)
        };
    }
    DeadEndDetector is_dead_end = [&heuristic] (const State &state) {
                                      return heuristic(state) == INF;
                                  };
    RandomWalkSampler sampler(task_proxy, init_h, rng, is_dead_end);

    vector<State> samples;
    while (static_cast<int>(samples.size()) < num_samples) {
        State sample = sampler.sample_state();
        assert(sample == initial_state || heuristic(sample) != INF);
        samples.push_back(move(sample));
    }

    cout << "Samples: " << samples.size() << endl;
    cout << "Sampling time: " << sampling_timer << endl;

    return samples;
}

void reduce_costs(vector<int> &remaining_costs, const vector<int> &saturated_costs) {
    assert(remaining_costs.size() == saturated_costs.size());
    for (size_t i = 0; i < remaining_costs.size(); ++i) {
        int &remaining = remaining_costs[i];
        const int &saturated = saturated_costs[i];
        assert(saturated <= remaining);
        /* Since we ignore transitions from states s with h(s)=INF, all
           saturated costs (h(s)-h(s')) are finite or -INF. */
        assert(saturated != INF);
        if (remaining == INF) {
            // INF - x = INF for finite values x.
        } else if (saturated == -INF) {
            remaining = INF;
        } else {
            remaining -= saturated;
        }
        assert(remaining >= 0);
    }
}

vector<bool> convert_to_bitvector(const vector<int> &vec, int size) {
    vector<bool> bitvector(size, false);
    for (int value : vec) {
        bitvector[value] = true;
    }
    return bitvector;
}
}
