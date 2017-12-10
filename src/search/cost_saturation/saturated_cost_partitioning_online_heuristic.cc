#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
#include "cost_partitioning_collection_generator.h"
#include "cost_partitioning_generator.h"
#include "cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(
    const options::Options &opts,
    Abstractions &&abstractions,
    CPHeuristics &&cp_heuristics)
    : Heuristic(opts),
      cp_generator(opts.get<shared_ptr<CostPartitioningGenerator>>("orders")),
      abstractions(move(abstractions)),
      cp_heuristics(move(cp_heuristics)),
      interval(opts.get<int>("interval")),
      store_cost_partitionings(opts.get<bool>("store_cost_partitionings")),
      costs(task_properties::get_operator_costs(task_proxy)),
      num_evaluated_states(0),
      num_scps_computed(0) {
    seen_facts.resize(task_proxy.get_variables().size());
    for (VariableProxy var : task_proxy.get_variables()) {
        seen_facts[var.get_id()].resize(var.get_domain_size(), false);
    }
}

bool SaturatedCostPartitioningOnlineHeuristic::should_compute_scp(const State &state) {
    if (interval > 0) {
        return num_evaluated_states % interval == 0;
    } else if (interval == -1) {
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

int SaturatedCostPartitioningOnlineHeuristic::compute_heuristic(
    const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    ++num_evaluated_states;
    vector<int> local_state_ids = get_local_state_ids(abstractions, state);
    int max_h = compute_max_h_with_statistics(
        cp_heuristics, local_state_ids, num_best_order);
    if (max_h == INF) {
        return DEAD_END;
    }

    if (should_compute_scp(state)) {
        Order order = cp_generator->get_next_order(
            task_proxy, abstractions, costs, local_state_ids, num_evaluated_states == 0);
        CostPartitionedHeuristic cost_partitioning =
            compute_saturated_cost_partitioning(abstractions, order, costs, true);
        ++num_scps_computed;
        int single_h = cost_partitioning.compute_heuristic(local_state_ids);
        if (store_cost_partitionings && single_h > max_h) {
            cp_heuristics.push_back(move(cost_partitioning));
        }
        if (single_h == INF) {
            return DEAD_END;
        }
        return max(max_h, single_h);
    }
    return max_h;
}

void SaturatedCostPartitioningOnlineHeuristic::print_statistics() const {
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

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"));

    return new SaturatedCostPartitioningOnlineHeuristic(
        opts,
        move(abstractions),
        get_cp_collection_generator_from_options(opts).get_cost_partitionings(
            task_proxy, abstractions, costs, compute_saturated_cost_partitioning));
}

static Plugin<Heuristic> _plugin("saturated_cost_partitioning_online", _parse);
}
