#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "scp_generators.h"
#include "types.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
// TODO: Use version from task_tools.
static vector<int> get_operator_costs(const TaskProxy &task) {
    vector<int> costs;
    costs.reserve(task.get_operators().size());
    for (OperatorProxy op : task.get_operators())
        costs.push_back(op.get_cost());
    return costs;
}

SaturatedCostPartitioningHeuristic::SaturatedCostPartitioningHeuristic(const Options &opts)
    : Heuristic(opts) {
    vector<unique_ptr<Abstraction>> abstractions;
    for (const shared_ptr<AbstractionGenerator> &generator :
         opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators")) {
        for (AbstractionAndStateMap &pair : generator->generate_abstractions(task)) {
            abstractions.push_back(move(pair.first));
            state_maps.push_back(move(pair.second));
        }
    }

    utils::Timer scp_timer;
    const vector<int> costs = get_operator_costs(task_proxy);
    h_values_by_order = opts.get<shared_ptr<SCPGenerator>>(
        "orders")->get_cost_partitionings(task_proxy, abstractions, state_maps, costs);
    num_best_order.resize(h_values_by_order.size(), 0);

    cout << "Time for computing cost partitionings: " << scp_timer << endl;
    cout << "Abstractions: " << abstractions.size() << endl;
    cout << "Orders: " << h_values_by_order.size() << endl;
}

int SaturatedCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int SaturatedCostPartitioningHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(state_maps, state);
    int max_h = compute_max_h_with_statistics(local_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int SaturatedCostPartitioningHeuristic::compute_max_h_with_statistics(
    const vector<int> &local_state_ids) const {
    int max_h = 0;
    int best_id = -1;
    int current_id = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
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

    assert(best_id != -1 || h_values_by_order.empty());
    if (best_id != -1) {
        assert(utils::in_bounds(best_id, num_best_order));
        ++num_best_order[best_id];
    }

    return max_h;
}

void SaturatedCostPartitioningHeuristic::print_statistics() const {
    int num_superfluous = count(num_best_order.begin(), num_best_order.end(), 0);
    int num_orders = num_best_order.size();
    double percentage_superfluous =
        (num_orders == 0) ? 0 : num_superfluous * 100.0 / num_orders;
    cout << "Number of times each order was the best order: "
         << num_best_order << endl;
    cout << "Superfluous orders: " << num_superfluous << "/" << num_orders
         << " = " << percentage_superfluous << endl;
}

static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning heuristic",
        "");

    parser.document_language_support("action costs", "supported");
    parser.document_language_support(
        "conditional effects",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_language_support(
        "axioms",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_property("admissible", "yes");
    parser.document_property(
        "consistent",
        "yes, if all abstraction generators represent consistent heuristics");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    parser.add_list_option<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators",
        "methods that generate abstractions");
    parser.add_option<shared_ptr<SCPGenerator>>(
        "orders",
        "saturated cost partitioning generator",
        "random(1)");
    Heuristic::add_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    opts.verify_list_non_empty<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators");

    if (parser.dry_run())
        return nullptr;

    return new SaturatedCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("saturated_cost_partitioning", _parse);
}
