#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts),
      cp_generator(opts.get<shared_ptr<CostPartitioningGenerator>>("orders")),
      interval(opts.get<int>("interval")),
      store_cost_partitionings(opts.get<bool>("store_cost_partitionings")),
      costs(get_operator_costs(task_proxy)),
      num_evaluated_states(0),
      num_scps_computed(0) {
    const bool verbose = debug;

    seen_facts.resize(task_proxy.get_variables().size());
    for (VariableProxy var : task_proxy.get_variables()) {
        seen_facts[var.get_id()].resize(var.get_domain_size(), false);
    }

    vector<int> costs = get_operator_costs(task_proxy);
    h_values_by_order =
        cp_generator->get_cost_partitionings(
            task_proxy, abstractions, costs,
            [verbose](const Abstractions &abstractions, const vector<int> &order, const vector<int> &costs) {
            return compute_saturated_cost_partitioning(abstractions, order, costs, verbose);
        });
    num_best_order.resize(h_values_by_order.size(), 0);
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
            h_values_by_order.push_back(cost_partitioning);
            num_best_order.push_back(0);
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
