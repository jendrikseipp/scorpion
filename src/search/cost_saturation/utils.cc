#include "utils.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_collection_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "order_generator_greedy.h"

#include "../task_proxy.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../task_utils/sampling.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"

#include <cassert>
#include <numeric>

using namespace std;

namespace cost_saturation {
std::vector<Order> systematic_generator_orders_hacked;

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

Order get_default_order(int num_abstractions) {
    vector<int> indices(num_abstractions);
    iota(indices.begin(), indices.end(), 0);
    return indices;
}

int compute_sum_h(
    const vector<int> &abstract_state_ids,
    const vector<vector<int>> &h_values_by_abstraction) {
    int sum_h = 0;
    assert(abstract_state_ids.size() == h_values_by_abstraction.size());
    for (size_t i = 0; i < abstract_state_ids.size(); ++i) {
        const vector<int> &h_values = h_values_by_abstraction[i];
        int state_id = abstract_state_ids[i];
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
    const vector<int> &abstract_state_ids,
    vector<int> &num_best_order) {
    int max_h = 0;
    int best_id = -1;
    int current_id = 0;
    for (const CostPartitioningHeuristic &cp_heuristic : cp_heuristics) {
        int sum_h = cp_heuristic.compute_heuristic(abstract_state_ids);
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

vector<int> get_abstract_state_ids(
    const Abstractions &abstractions, const State &state) {
    vector<int> abstract_state_ids;
    abstract_state_ids.reserve(abstractions.size());
    for (auto &abstraction : abstractions) {
        if (abstraction) {
            // Only add local state IDs for useful abstractions.
            abstract_state_ids.push_back(abstraction->get_abstract_state_id(state));
        } else {
            // Add dummy value if abstraction will never be used.
            abstract_state_ids.push_back(-1);
        }
    }
    return abstract_state_ids;
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
}
