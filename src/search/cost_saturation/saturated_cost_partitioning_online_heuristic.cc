#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_collection_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
static CostPartitioning compute_saturated_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool debug) {
    assert(abstractions.size() == order.size());
    vector<vector<int>> h_values_by_abstraction(abstractions.size());
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
        h_values_by_abstraction[pos] = move(h_values);
        reduce_costs(remaining_costs, saturated_costs);
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return h_values_by_abstraction;
}

SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts),
      cp_generator(opts.get<shared_ptr<CostPartitioningGenerator>>("orders")),
      interval(opts.get<int>("interval")),
      store_cost_partitionings(opts.get<bool>("store_cost_partitionings")),
      filter_blind_heuristics(opts.get<bool>("filter_zero_h_values")),
      costs(get_operator_costs(task_proxy)),
      num_evaluated_states(0),
      num_scps_computed(0) {
    const bool verbose = debug;

    seen_facts.resize(task_proxy.get_variables().size());
    for (VariableProxy var : task_proxy.get_variables()) {
        seen_facts[var.get_id()].resize(var.get_domain_size(), false);
    }

    CostPartitioningCollectionGenerator cps_generator(
        cp_generator,
        opts.get<int>("max_orders"),
        opts.get<double>("max_time"),
        opts.get<bool>("diversify"),
        utils::parse_rng_from_options(opts));
    vector<int> costs = get_operator_costs(task_proxy);
    cp_heuristics =
        cps_generator.get_cost_partitionings(
            task_proxy, abstractions, costs,
            [verbose](const Abstractions &abstractions, const vector<int> &order, const vector<int> &costs) {
            return compute_saturated_cost_partitioning(abstractions, order, costs, verbose);
        }, filter_blind_heuristics);
}

bool SaturatedCostPartitioningOnlineHeuristic::should_compute_scp(const State &state) {
    if (interval > 0) {
        return num_evaluated_states % interval == 0;
    } else if (interval == -1 ) {
        bool novel = false;
        for (FactProxy fact_proxy : state) {
            FactPair fact = fact_proxy.get_pair();
            if (!seen_facts[fact.var][fact.value]) {
                novel = true;
                seen_facts[fact.var][fact.value] = true;
            }
        }
        return novel;
    } else {
        ABORT("invalid value for interval");
    }
}

int SaturatedCostPartitioningOnlineHeuristic::compute_heuristic(const State &state) {
    ++num_evaluated_states;
    vector<int> local_state_ids = get_local_state_ids(abstractions, state);
    int max_h = compute_max_h_with_statistics(local_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }

    if (should_compute_scp(state)) {
        const bool verbose = debug;
        CostPartitioning cost_partitioning = cp_generator->get_next_cost_partitioning(
            task_proxy, abstractions, costs, state,
            [verbose](const Abstractions &abstractions, const vector<int> &order, const vector<int> &costs) {
            return compute_saturated_cost_partitioning(abstractions, order, costs, verbose);
        });
        ++num_scps_computed;
        int single_h = compute_sum_h(local_state_ids, cost_partitioning);
        assert(single_h != INF);
        if (store_cost_partitionings && single_h > max_h) {
            cp_heuristics.emplace_back(move(cost_partitioning), filter_blind_heuristics);
        }
        return max(max_h, single_h);
    }
    return max_h;
}

void SaturatedCostPartitioningOnlineHeuristic::print_statistics() const {
    CostPartitioningHeuristic::print_statistics();
    cout << "Computed SCPs: " << num_scps_computed << endl;
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning online heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_cost_partitioning_collection_options_to_parser(parser);

    parser.add_option<int>(
        "interval",
        "compute SCP for every interval-th state",
        OptionParser::NONE,
        Bounds("-1", "infinity"));
    parser.add_option<bool>(
        "store_cost_partitionings",
        "store saturated cost partitionings if diverse",
        OptionParser::NONE);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return new SaturatedCostPartitioningOnlineHeuristic(opts);
}

static Plugin<Heuristic> _plugin("saturated_cost_partitioning_online", _parse);
}
