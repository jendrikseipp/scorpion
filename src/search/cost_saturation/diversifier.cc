#include "diversifier.h"

#include "abstraction.h"
#include "cost_partitioning_generator.h"
#include "utils.h"

#include "../task_proxy.h"

#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/countdown_timer.h"

#include <cassert>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
Diversifier::Diversifier(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &costs)
    : portfolio_h_values(max_samples, -1) {
    // Always use h(s_0) from SCP to obtain the same samples for all CPs.
    vector<int> default_order = get_default_order(abstractions.size());
    CostPartitioning scp_for_default_order =
        compute_saturated_cost_partitioning(abstractions, default_order, costs);

    function<int (const State &state)> default_order_heuristic =
            [&abstractions,&scp_for_default_order](const State &state) {
        vector<int> local_state_ids = get_local_state_ids(abstractions, state);
        return compute_sum_h(local_state_ids, scp_for_default_order);
    };

    vector<State> samples = sample_states(
        task_proxy, default_order_heuristic, max_samples);

    for (const State &sample : samples) {
        local_state_ids_by_sample.push_back(
            get_local_state_ids(abstractions, sample));
    }
    utils::release_vector_memory(samples);

    // Log percentage of abstract states covered by samples.
    int num_abstract_states = 0;
    int num_covered_states = 0;
    for (size_t i = 0; i < abstractions.size(); ++i) {
        const Abstraction &abstraction = *abstractions[i];
        unordered_set<int> covered_states;
        for (size_t j = 0; j < local_state_ids_by_sample.size(); ++j) {
            const vector<int> &local_ids = local_state_ids_by_sample[j];
            covered_states.insert(local_ids[i]);
        }
        num_abstract_states += abstraction.get_num_states();
        num_covered_states += covered_states.size();
    }
    cout << "Covered abstract states: "
         << num_covered_states << "/" << num_abstract_states << " = "
         << (num_abstract_states ?
             static_cast<double>(num_covered_states) / num_abstract_states : 1)
         << endl;
}

bool Diversifier::is_diverse(const CostPartitioning &scp) {
    bool scp_improves_portfolio = false;
    for (size_t sample_id = 0; sample_id < local_state_ids_by_sample.size(); ++sample_id) {
        int scp_h_value = compute_sum_h(local_state_ids_by_sample[sample_id], scp);
        assert(utils::in_bounds(sample_id, portfolio_h_values));
        int &portfolio_h_value = portfolio_h_values[sample_id];
        if (scp_h_value > portfolio_h_value) {
            scp_improves_portfolio = true;
            portfolio_h_value = scp_h_value;
        }
    }

    // Statistics.
    if (scp_improves_portfolio) {
        int sum_portfolio_h = 0;
        for (int h : portfolio_h_values) {
            sum_portfolio_h += h;
        }
        cout << "Portfolio sum h value: " << sum_portfolio_h << endl;
    }

    return scp_improves_portfolio;
}
}
