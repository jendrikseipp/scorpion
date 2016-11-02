#include "utils.h"

#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_proxy.h"
#include "../task_tools.h"

#include "../utils/collections.h"
#include "../utils/countdown_timer.h"

#include <algorithm>
#include <cassert>

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
        int state_id = local_state_ids[i];
        if (state_id == -1) {
            // Abstract state has been pruned.
            return INF;
        }
        const vector<int> &h_values = h_values_by_abstraction[i];
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
    const vector<StateMap> &state_maps, const State &state) {
    vector<int> local_state_ids;
    local_state_ids.reserve(state_maps.size());
    for (auto &state_map : state_maps) {
        local_state_ids.push_back(state_map(state));
    }
    return local_state_ids;
}

vector<State> sample_states(
    const TaskProxy &task_proxy,
    const function<int (const State &state)> &heuristic,
    int num_samples) {
    cout << "Start sampling" << endl;
    utils::CountdownTimer sampling_timer(60);

    SuccessorGenerator successor_generator(task_proxy);
    const double average_operator_costs = get_average_operator_cost(task_proxy);
    State initial_state = task_proxy.get_initial_state();
    int init_h = heuristic(initial_state);
    cout << "Initial h value for default order: " << init_h << endl;

    vector<State> samples;
    while (init_h != INF &&
           static_cast<int>(samples.size()) < num_samples &&
           !sampling_timer.is_expired()) {
        State sample = sample_state_with_random_walk(
            initial_state,
            successor_generator,
            init_h,
            average_operator_costs);
        if (heuristic(sample) != INF) {
            samples.push_back(move(sample));
        }
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

void print_indexed_vector(const vector<int> &vec) {
    for (size_t i = 0; i < vec.size(); ++i) {
        cout << i << ":" << vec[i] << ", ";
    }
    cout << endl;
}
}
