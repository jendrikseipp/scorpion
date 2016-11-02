#include "scp_generator.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/logging.h"
#include "../utils/countdown_timer.h"

#include <cassert>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
vector<vector<int>> compute_saturated_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs) {
    assert(abstractions.size() == order.size());
    const bool debug = false;
    vector<int> remaining_costs = costs;
    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
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
        h_values_by_abstraction[pos] = move(h_values);
        reduce_costs(remaining_costs, saturated_costs);
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return h_values_by_abstraction;
}


class Diversifier {
    const int max_samples = 1000;
    vector<int> portfolio_h_values;
    vector<vector<int>> local_state_ids_by_sample;

public:
    Diversifier(
        const TaskProxy &task_proxy,
        const vector<unique_ptr<Abstraction>> &abstractions,
        const vector<StateMap> &state_maps,
        const vector<int> &costs);

    bool is_diverse(const CostPartitioning &scp);
};

Diversifier::Diversifier(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<StateMap> &state_maps,
    const vector<int> &costs)
    : portfolio_h_values(max_samples, -1) {
    vector<int> default_order = get_default_order(abstractions.size());
    CostPartitioning scp_for_default_order =
        compute_saturated_cost_partitioning(abstractions, default_order, costs);

    function<int (const State &state)> default_order_heuristic =
            [&state_maps,&scp_for_default_order](const State &state) {
        vector<int> local_state_ids = get_local_state_ids(state_maps, state);
        return compute_sum_h(local_state_ids, scp_for_default_order);
    };

    vector<State> samples = sample_states(
        task_proxy, default_order_heuristic, max_samples);

    for (const State &sample : samples) {
        local_state_ids_by_sample.push_back(
            get_local_state_ids(state_maps, sample));
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


SCPGenerator::SCPGenerator(const Options &opts)
    : max_orders(opts.get<int>("max_orders")),
      max_time(opts.get<double>("max_time")),
      diversify(opts.get<bool>("diversify")) {
}

void SCPGenerator::initialize(
    const TaskProxy &,
    const vector<unique_ptr<Abstraction>> &,
    const vector<StateMap> &,
    const vector<int> &) {
    // Do nothing by default.
}

CostPartitionings SCPGenerator::get_cost_partitionings(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<StateMap> &state_maps,
    const vector<int> &costs) {
    initialize(task_proxy, abstractions, state_maps, costs);

    unique_ptr<Diversifier> diversifier;
    if (diversify) {
        diversifier = utils::make_unique_ptr<Diversifier>(
            task_proxy, abstractions, state_maps, costs);
    }
    CostPartitionings cost_partitionings;
    utils::CountdownTimer timer(max_time);
    int evaluated_orders = 0;
    while (static_cast<int>(cost_partitionings.size()) < max_orders &&
           !timer.is_expired() && has_next_cost_partitioning()) {

        CostPartitioning scp = get_next_cost_partitioning(
            task_proxy, abstractions, state_maps, costs);
        ++evaluated_orders;
        if (!diversify || diversifier->is_diverse(scp)) {
            cost_partitionings.push_back(move(scp));
        }
    }
    cout << "Total evaluated orders: " << evaluated_orders << endl;
    return cost_partitionings;
}


void add_common_scp_generator_options_to_parser(OptionParser &parser) {
    parser.add_option<int>(
        "max_orders",
        "maximum number of abstraction orders",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time for finding cost partitionings",
        "10",
        Bounds("0", "infinity"));
    parser.add_option<bool>(
        "diversify",
        "only keep diverse orders",
        "true");
}


static PluginTypePlugin<SCPGenerator> _type_plugin(
    "SCPGenerator",
    "Saturated cost partitioning generator.");
}
