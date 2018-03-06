#include "diversifier.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
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
    function<int (const State &state)> sampling_heuristic,
    int num_samples,
    const shared_ptr<utils::RandomNumberGenerator> &rng) {
    vector<State> samples = sample_states(
        task_proxy, sampling_heuristic, num_samples, rng);

    for (const State &sample : samples) {
        local_state_ids_by_sample.push_back(
            get_local_state_ids(abstractions, sample));
    }
    utils::release_vector_memory(samples);

    // Initialize portfolio h values with -1 to ensure that first CP is diverse.
    portfolio_h_values.resize(local_state_ids_by_sample.size(), -1);

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
    utils::Log() << "Covered abstract states: "
                 << num_covered_states << "/" << num_abstract_states << " = "
                 << (num_abstract_states ?
        static_cast<double>(num_covered_states) / num_abstract_states : 1)
                 << endl;
}

bool Diversifier::is_diverse(const CostPartitionedHeuristic &cp) {
    bool cp_improves_portfolio = false;
    for (size_t sample_id = 0; sample_id < local_state_ids_by_sample.size(); ++sample_id) {
        int cp_h_value = cp.compute_heuristic(local_state_ids_by_sample[sample_id]);
        assert(utils::in_bounds(sample_id, portfolio_h_values));
        int &portfolio_h_value = portfolio_h_values[sample_id];
        if (cp_h_value > portfolio_h_value) {
            cp_improves_portfolio = true;
            portfolio_h_value = cp_h_value;
        }
    }

    // Statistics.
    if (cp_improves_portfolio) {
        int sum_portfolio_h = 0;
        for (int h : portfolio_h_values) {
            sum_portfolio_h += h;
        }
        utils::Log() << "Portfolio sum h value: " << sum_portfolio_h << endl;
    }

    return cp_improves_portfolio;
}
}
