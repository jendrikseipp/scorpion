#include "scp_optimizer.h"

#include "abstraction.h"
#include "cost_saturation.h"
#include "utils.h"

#include "../globals.h"

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

static vector<int> get_shuffled_order(int n) {
    vector<int> order = get_default_order(n);
    g_rng()->shuffle(order);
    return order;
}

static vector<vector<int>> get_local_state_ids_by_state(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const vector<State> &states) {
    vector<vector<int>> local_state_ids_by_state;
    for (const State &state : states) {
        local_state_ids_by_state.push_back(
                    get_local_state_ids(subtasks, refinement_hierarchies, state));
    }
    return local_state_ids_by_state;
}


SCPOptimizer::SCPOptimizer(
    vector<unique_ptr<Abstraction>> &&abstractions,
    vector<shared_ptr<AbstractTask>> &&subtasks,
    vector<shared_ptr<RefinementHierarchy>> &&refinement_hierarchies,
    const vector<int> &operator_costs,
    const vector<State> &states)
    : abstractions(move(abstractions)),
      subtasks(move(subtasks)),
      refinement_hierarchies(move(refinement_hierarchies)),
      local_state_ids_by_state(get_local_state_ids_by_state(
          subtasks, refinement_hierarchies, states)),
      operator_costs(operator_costs),
      incumbent_order(get_shuffled_order(subtasks.size())),
      incumbent_total_h_value(evaluate(incumbent_order)) {
    while (search_improving_successor()) {
        cout << "Incumbent total h value: " << incumbent_total_h_value << endl;
    }
}

int SCPOptimizer::evaluate(const vector<int> &order) const {
    vector<vector<int>> h_values_by_abstraction =
        compute_saturated_cost_partitioning(abstractions, order, operator_costs);
    int total_h = 0;
    for (size_t sample_id = 0; sample_id < local_state_ids_by_state.size(); ++sample_id) {
        // TODO: handle INF.
        int sum_h = compute_sum_h(
                    local_state_ids_by_state[sample_id],
                    h_values_by_abstraction);
        total_h += sum_h;
    }
    return total_h;
}

bool SCPOptimizer::search_improving_successor() {
    int num_samples = local_state_ids_by_state.size();
    for (int i = 0; i < num_samples; ++i) {
        for (int j = i + 1; j < num_samples; ++j) {
            swap(incumbent_order[i], incumbent_order[j]);
            int total_h = evaluate(incumbent_order);
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

vector<int> SCPOptimizer::extract_order() {
    assert(!incumbent_order.empty());
    return move(incumbent_order);
}


vector<vector<int> > compute_saturated_cost_partitioning(
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
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const vector<shared_ptr<RefinementHierarchy>> &refinement_hierarchies,
    const State &state) {
    assert(subtasks.size() == refinement_hierarchies.size());
    vector<int> local_state_ids;
    local_state_ids.reserve(subtasks.size());
    for (size_t i = 0; i < subtasks.size(); ++i) {
        const AbstractTask &subtask = *subtasks[i];
        TaskProxy subtask_proxy(subtask);
        State local_state = subtask_proxy.convert_ancestor_state(state);
        local_state_ids.push_back(
                    refinement_hierarchies[i]->get_node(local_state)->get_state_id());
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
}
