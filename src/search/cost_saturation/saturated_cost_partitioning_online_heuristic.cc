#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "order_generator.h"
#include "saturated_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/rng_options.h"
#include "../utils/timer.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(
    const options::Options &opts,
    Abstractions &&abstractions,
    CPHeuristics &&cp_heuristics,
    UnsolvabilityHeuristic &&unsolvability_heuristic)
    : Heuristic(opts),
      cp_generator(opts.get<shared_ptr<OrderGenerator>>("orders")),
      abstractions(move(abstractions)),
      cp_heuristics(move(cp_heuristics)),
      unsolvability_heuristic(move(unsolvability_heuristic)),
      interval(opts.get<int>("interval")),
      skip_seen_orders(opts.get<bool>("skip_seen_orders")),
      max_time(opts.get<double>("max_time")),
      diversify(opts.get<bool>("diversify")),
      costs(task_properties::get_operator_costs(task_proxy)),
      num_evaluated_states(0),
      num_scps_computed(0) {
    seen_facts.resize(task_proxy.get_variables().size());
    for (VariableProxy var : task_proxy.get_variables()) {
        seen_facts[var.get_id()].resize(var.get_domain_size(), false);
    }

    timer = utils::make_unique_ptr<utils::Timer>();
    timer->stop();
}

SaturatedCostPartitioningOnlineHeuristic::~SaturatedCostPartitioningOnlineHeuristic() {
    print_statistics();
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
    vector<int> abstract_state_ids = get_abstract_state_ids(abstractions, state);
    if (unsolvability_heuristic.is_unsolvable(abstract_state_ids)) {
        return DEAD_END;
    }

    int max_h = compute_max_h_with_statistics(
        cp_heuristics, abstract_state_ids, num_best_order);

    if ((*timer)() > max_time) {
        return max_h;
    }

    if (should_compute_scp(state)) {
        timer->resume();
        Order order = cp_generator->compute_order_for_state(
            abstract_state_ids, num_evaluated_states == 0);
        if (skip_seen_orders) {
            if (!seen_orders.count(order)) {
                CostPartitioningHeuristic cost_partitioning =
                    compute_saturated_cost_partitioning(abstractions, order, costs);
                ++num_scps_computed;
                int h = cost_partitioning.compute_heuristic(abstract_state_ids);
                bool is_diverse = h > max_h;
                if (!diversify || is_diverse) {
                    cp_heuristics.push_back(move(cost_partitioning));
                }
                max_h = max(max_h, h);
                seen_orders.insert(move(order));
            }
        } else {
            CostPartitioningHeuristic cost_partitioning =
                compute_saturated_cost_partitioning(abstractions, order, costs);
            ++num_scps_computed;
            int h = cost_partitioning.compute_heuristic(abstract_state_ids);
            max_h = max(max_h, h);
        }
        timer->stop();
    }
    return max_h;
}

void SaturatedCostPartitioningOnlineHeuristic::print_statistics() const {
    cout << "Computed SCPs: " << num_scps_computed << endl;
    cout << "Stored SCPs: " << cp_heuristics.size() << endl;
    cout << "Time for computing SCPs: " << *timer << endl;
}


static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning online heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_order_options_to_parser(parser);

    parser.add_option<int>(
        "interval",
        "compute SCP for every interval-th state",
        "1",
        Bounds("-1", "infinity"));
    parser.add_option<bool>(
        "skip_seen_orders",
        "compute SCP only once for each order",
        "true");
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for computing cost partitionings",
        "infinity",
        Bounds("0", "infinity"));
    parser.add_option<bool>(
        "diversify",
        "only store cost partitionings that have a higher heuristic value for "
        "the evaluated state than all previously stored cost partitionings",
        "false");

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"));
    UnsolvabilityHeuristic unsolvability_heuristic(abstractions);
    CPHeuristics cp_heuristics =
        get_cp_heuristic_collection_generator_from_options(opts).generate_cost_partitionings(
            task_proxy, abstractions, costs, compute_saturated_cost_partitioning,
            unsolvability_heuristic);

    return make_shared<SaturatedCostPartitioningOnlineHeuristic>(
        opts,
        move(abstractions),
        move(cp_heuristics),
        move(unsolvability_heuristic));
}

static Plugin<Evaluator> _plugin("scp_online", _parse);
}
