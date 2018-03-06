#include "utils.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "cost_partitioned_heuristic.h"
#include "cost_partitioning_collection_generator.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_generator_greedy.h"

#include "../task_proxy.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../task_utils/sampling.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/timer.h"

#include <cassert>
#include <numeric>

using namespace std;

namespace cost_saturation {
Heuristic *get_max_cp_heuristic(
    options::OptionParser &parser, CPFunction cp_function) {
    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_cost_partitioning_collection_options_to_parser(parser);

    options::Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"));
    return new CostPartitioningHeuristic(
        opts,
        move(abstractions),
        get_cp_collection_generator_from_options(opts).get_cost_partitionings(
            task_proxy, abstractions, costs, cp_function));
}

Abstractions generate_abstractions(
    const shared_ptr<AbstractTask> &task,
    const vector<shared_ptr<AbstractionGenerator>> &abstraction_generators) {
    Abstractions abstractions;
    vector<int> abstractions_per_generator;
    for (const shared_ptr<AbstractionGenerator> &generator : abstraction_generators) {
        int abstractions_before = abstractions.size();
        for (auto &abstraction : generator->generate_abstractions(task)) {
            abstractions.push_back(move(abstraction));
        }
        abstractions_per_generator.push_back(abstractions.size() - abstractions_before);
    }
    utils::Log() << "Abstractions: " << abstractions.size() << endl;
    utils::Log() << "Abstractions per generator: " << abstractions_per_generator << endl;
    return abstractions;
}

vector<int> get_default_order(int num_abstractions) {
    vector<int> indices(num_abstractions);
    iota(indices.begin(), indices.end(), 0);
    assert(utils::is_sorted_unique(indices));
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

int compute_max_h_with_statistics(
    const CPHeuristics &cp_heuristics,
    const vector<int> &local_state_ids,
    vector<int> &num_best_order) {
    int max_h = 0;
    int best_id = -1;
    int current_id = 0;
    for (const CostPartitionedHeuristic &cp_heuristic : cp_heuristics) {
        int sum_h = cp_heuristic.compute_heuristic(local_state_ids);
        if (sum_h > max_h) {
            max_h = sum_h;
            best_id = current_id;
        }
        if (sum_h == INF) {
            break;
        }
        ++current_id;
    }
    assert(max_h >= 0);

    num_best_order.resize(cp_heuristics.size(), 0);
    if (best_id != -1) {
        assert(utils::in_bounds(best_id, num_best_order));
        ++num_best_order[best_id];
    }

    return max_h;
}

vector<int> get_local_state_ids(
    const Abstractions &abstractions, const State &state) {
    vector<int> local_state_ids;
    local_state_ids.reserve(abstractions.size());
    for (auto &abstraction : abstractions) {
        if (abstraction) {
            // Only add local state IDs for useful abstractions.
            local_state_ids.push_back(abstraction->get_abstract_state_id(state));
        } else {
            // Add dummy value if abstraction will never be used.
            local_state_ids.push_back(-1);
        }
    }
    return local_state_ids;
}

CostPartitionedHeuristic compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool sparse) {
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
        cp_heuristic.add_cp_heuristic_values(pos, move(h_values), sparse);
        reduce_costs(remaining_costs, saturated_costs);
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return cp_heuristic;
}

vector<State> sample_states(
    const TaskProxy &task_proxy,
    const function<int (const State &state)> &heuristic,
    int num_samples,
    const shared_ptr<utils::RandomNumberGenerator> &rng) {
    assert(num_samples >= 1);
    utils::Timer sampling_timer;
    utils::Log() << "Start sampling" << endl;
    State initial_state = task_proxy.get_initial_state();
    int init_h = heuristic(initial_state);
    utils::Log() << "Initial h value for sampling: " << init_h << endl;
    if (init_h == INF) {
        return {
                   move(initial_state)
        };
    }
    DeadEndDetector is_dead_end = [&heuristic] (const State &state) {
                                      return heuristic(state) == INF;
                                  };
    sampling::RandomWalkSampler sampler(task_proxy, init_h, rng, is_dead_end);

    vector<State> samples;
    while (static_cast<int>(samples.size()) < num_samples) {
        State sample = sampler.sample_state();
        assert(sample == initial_state || heuristic(sample) != INF);
        samples.push_back(move(sample));
    }

    utils::Log() << "Samples: " << samples.size() << endl;
    utils::Log() << "Sampling time: " << sampling_timer << endl;

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
