#include "scp_optimizer.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "utils.h"

#include "../globals.h"

#include "../utils/logging.h"
#include "../utils/rng.h"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace std;

namespace cegar {
vector<int> get_default_order(int n) {
    vector<int> indices(n);
    iota(indices.begin(), indices.end(), 0);
    return indices;
}

static vector<vector<int>> get_local_state_ids_by_state(
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const vector<State> &states) {
    vector<vector<int>> local_state_ids_by_state;
    for (const State &state : states) {
        local_state_ids_by_state.push_back(
            get_local_state_ids(refinement_hierarchies, state));
    }
    return local_state_ids_by_state;
}


SCPOptimizer::SCPOptimizer(
    vector<unique_ptr<Abstraction>> &&abstractions,
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const vector<int> &operator_costs)
    : abstractions(move(abstractions)),
      refinement_hierarchies(refinement_hierarchies),
      operator_costs(operator_costs) {
}

int SCPOptimizer::evaluate(
    const vector<int> &order,
    const vector<vector<vector<int>>> &h_values_by_orders) const {
    assert(!local_state_ids_by_state.empty());
    vector<vector<int>> h_values_by_abstraction =
        compute_saturated_cost_partitioning(abstractions, order, operator_costs);
    int total_h = 0;
    for (size_t sample_id = 0; sample_id < local_state_ids_by_state.size(); ++sample_id) {
        const vector<int> &local_state_ids = local_state_ids_by_state[sample_id];
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        if (sum_h == INF) {
            ABORT("Dead-end sample should have been filtered.");
        }
        int portfolio_sum_h = compute_max_h(local_state_ids, h_values_by_orders);
        if (h_values_by_orders.empty())
            assert(portfolio_sum_h == 0);
        total_h += max(0, sum_h - portfolio_sum_h);
    }
    ++evaluations;
    return total_h;
}

bool SCPOptimizer::search_improving_successor(
    const vector<vector<vector<int>>> &h_values_by_orders) {
    int num_abstractions = abstractions.size();
    for (int i = 0; i < num_abstractions; ++i) {
        for (int j = i + 1; j < num_abstractions; ++j) {
            swap(incumbent_order[i], incumbent_order[j]);
            int total_h = evaluate(incumbent_order, h_values_by_orders);
            if (total_h > incumbent_total_h_value) {
                // Set new incumbent.
                incumbent_total_h_value = total_h;
                return true;
            } else {
                // Restore incumbent order.
                swap(incumbent_order[i], incumbent_order[j]);
            }
        }
    }
    return false;
}

vector<vector<int>> SCPOptimizer::find_cost_partitioning(
    const vector<State> &states,
    double max_time,
    bool shuffle,
    const vector<vector<vector<int>>> &h_values_by_orders) {
    evaluations = 0;
    incumbent_order = get_default_order(abstractions.size());
    if (shuffle) {
        g_rng()->shuffle(incumbent_order);
    }
    utils::CountdownTimer timer(max_time);
    if (!states.empty()) {
        local_state_ids_by_state = get_local_state_ids_by_state(
            refinement_hierarchies, states);
        incumbent_total_h_value = evaluate(incumbent_order, h_values_by_orders);
        do {
            g_log << "Incumbent total h value: " << incumbent_total_h_value << endl;
            if (timer.is_expired()) {
                g_log << "Optimization time limit reached." << endl;
                break;
            }
        } while (search_improving_successor(h_values_by_orders));
    }
    g_log << "Order evaluations: " << evaluations << endl;
    return compute_saturated_cost_partitioning(
        abstractions, incumbent_order, operator_costs);
}


vector<vector<int>> compute_saturated_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &operator_costs) {
    assert(abstractions.size() == order.size());
    vector<int> remaining_costs = operator_costs;
    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
        abstraction.set_operator_costs(remaining_costs);
        h_values_by_abstraction[pos] = abstraction.get_h_values();
        reduce_costs(remaining_costs, abstraction.get_saturated_costs());
    }
    return h_values_by_abstraction;
}

vector<int> get_local_state_ids(
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const State &state) {
    vector<int> local_state_ids;
    local_state_ids.reserve(refinement_hierarchies.size());
    for (const shared_ptr<RefinementHierarchy> &hierarchy : refinement_hierarchies) {
        local_state_ids.push_back(hierarchy->get_local_state_id(state));
    }
    return local_state_ids;
}

int compute_sum_h(
        const vector<int> &local_state_ids,
        const vector<vector<int> > &h_values_by_abstraction) {
    int sum_h = 0;
    assert(local_state_ids.size() == h_values_by_abstraction.size());
    for (size_t i = 0; i < local_state_ids.size(); ++i) {
        int state_id = local_state_ids[i];
        const vector<int> &h_values = h_values_by_abstraction[i];
        assert(utils::in_bounds(state_id, h_values));
        int value = h_values[state_id];
        assert(value >= 0);
        if (value == INF)
            return INF;
        sum_h += value;
    }
    assert(sum_h >= 0);
    return sum_h;
}

int compute_max_h(
    const vector<int> &local_state_ids,
    const vector<vector<vector<int>>> &h_values_by_order) {
    int max_h = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        if (sum_h == INF) {
            return INF;
        }
        max_h = max(max_h, sum_h);
    }
    return max_h;
}
}
