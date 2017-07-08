#include "scp_optimizer.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "utils.h"

#include "../globals.h"

#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace std;

namespace cegar {
SCPOptimizer::SCPOptimizer(
    vector<unique_ptr<Abstraction>> &&abstractions,
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const vector<int> &operator_costs,
    const shared_ptr<utils::RandomNumberGenerator> &rng)
    : abstractions(move(abstractions)),
      refinement_hierarchies(refinement_hierarchies),
      operator_costs(operator_costs),
      rng(rng) {
    order_evaluation_timer = utils::make_unique_ptr<utils::Timer>();
    order_evaluation_timer->stop();
    scp_computation_timer = utils::make_unique_ptr<utils::Timer>();
    scp_computation_timer->stop();
}

int SCPOptimizer::evaluate(
    const vector<vector<int>> &h_values_by_abstraction,
    const vector<vector<int>> &local_state_ids_by_state,
    const vector<int> &portfolio_h_values,
    vector<int> &portfolio_h_values_improvement) const {
    assert(!local_state_ids_by_state.empty());

    order_evaluation_timer->resume();
    int total_h = 0;
    if (debug) {
        cout << "portfolio_h_values: " << portfolio_h_values << endl;
        cout << "portfolio_h_impr.:  " << portfolio_h_values_improvement << endl;
    }
    for (size_t sample_id = 0; sample_id < local_state_ids_by_state.size(); ++sample_id) {
        assert(utils::in_bounds(sample_id, local_state_ids_by_state));
        const vector<int> &local_state_ids = local_state_ids_by_state[sample_id];
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        assert(sum_h != INF);
        assert(utils::in_bounds(sample_id, portfolio_h_values));
        int portfolio_sum_h = portfolio_h_values[sample_id];
        assert(utils::in_bounds(sample_id, portfolio_h_values_improvement));
        int delta_to_portfolio = max(0, sum_h - portfolio_sum_h);
        portfolio_h_values_improvement[sample_id] = delta_to_portfolio;
        if (debug) {
            cout << "id: " << sample_id << endl;
            cout << "sum_h: " << sum_h << endl;
            cout << "portfolio_sum_h: " << portfolio_sum_h << endl;
            cout << "delta: " << delta_to_portfolio << endl;
        }
        total_h += delta_to_portfolio;
    }
    ++evaluations;
    order_evaluation_timer->stop();
    return total_h;
}

bool SCPOptimizer::search_improving_successor(
    const utils::CountdownTimer &timer,
    const vector<vector<int>> &local_state_ids_by_state,
    vector<int> &incumbent_order,
    int &incumbent_total_h_value,
    const vector<int> &portfolio_h_values,
    vector<int> &portfolio_h_values_improvement) const {
    int num_abstractions = abstractions.size();
    for (int i = 0; i < num_abstractions && !timer.is_expired(); ++i) {
        for (int j = i + 1; j < num_abstractions && !timer.is_expired(); ++j) {
            swap(incumbent_order[i], incumbent_order[j]);

            scp_computation_timer->resume();
            vector<vector<int>> h_values_by_abstraction =
                compute_saturated_cost_partitioning(
                    abstractions, incumbent_order, operator_costs);
            scp_computation_timer->stop();

            int total_h = evaluate(
                h_values_by_abstraction,
                local_state_ids_by_state,
                portfolio_h_values,
                portfolio_h_values_improvement);
            if (total_h > incumbent_total_h_value) {
                // Set new incumbent.
                incumbent_scp = move(h_values_by_abstraction);
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

pair<vector<vector<int>>, pair<int, int>> SCPOptimizer::find_cost_partitioning(
    const vector<vector<int>> &local_state_ids_by_state,
    double max_time,
    bool shuffle,
    bool reverse_order,
    const vector<int> &portfolio_h_values,
    vector<int> &portfolio_h_values_improvement) const {
    assert(local_state_ids_by_state.size() == portfolio_h_values.size());
    const bool using_hill_climbing = max_time != 0;
    utils::CountdownTimer timer(max_time);
    evaluations = 0;
    vector<int> incumbent_order = get_default_order(abstractions.size());
    if (shuffle) {
        rng->shuffle(incumbent_order);
    }
    if (reverse_order) {
        cout << "Landmark abstractions: " << hacked_num_landmark_abstractions
             << "/" << abstractions.size() << endl;
        reverse(incumbent_order.begin(), incumbent_order.begin() + hacked_num_landmark_abstractions);
        reverse(incumbent_order.begin() + hacked_num_landmark_abstractions, incumbent_order.end());
    }

    scp_computation_timer->resume();
    incumbent_scp = compute_saturated_cost_partitioning(
        abstractions, incumbent_order, operator_costs);
    scp_computation_timer->stop();

    int incumbent_total_h_value = 0;
    if (!local_state_ids_by_state.empty()) {
        if (debug) {
            cout << "Evaluate order: " << incumbent_order << endl;
        }
        incumbent_total_h_value = evaluate(
            incumbent_scp, local_state_ids_by_state,
            portfolio_h_values, portfolio_h_values_improvement);
        do {
            if (incumbent_total_h_value > 0) {
                g_log << "Found order with h = "
                      << incumbent_total_h_value << ": "
                      << incumbent_order << endl;
            }
        } while (
            !timer.is_expired() &&
            search_improving_successor(
                timer, local_state_ids_by_state,
                incumbent_order, incumbent_total_h_value,
                portfolio_h_values, portfolio_h_values_improvement));
        if (using_hill_climbing && timer.is_expired()) {
            g_log << "Optimization time limit reached." << endl;
        }
    }
    return make_pair(
        move(incumbent_scp),
        make_pair(incumbent_total_h_value, evaluations));
}


vector<int> get_default_order(int n) {
    vector<int> indices(n);
    iota(indices.begin(), indices.end(), 0);
    return indices;
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
    const vector<vector<int>> &h_values_by_abstraction) {
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
