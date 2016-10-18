#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace cost_saturation {
static vector<int> get_operator_costs(const TaskProxy &task) {
    vector<int> costs;
    costs.reserve(task.get_operators().size());
    for (OperatorProxy op : task.get_operators())
        costs.push_back(op.get_cost());
    return costs;
}

static vector<int> get_default_order(int n) {
    vector<int> indices(n);
    iota(indices.begin(), indices.end(), 0);
    return indices;
}

static void reduce_costs(
    vector<int> &remaining_costs, const vector<int> &saturated_costs) {
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

static vector<vector<int>> compute_saturated_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &operator_costs) {
    assert(abstractions.size() == order.size());
    vector<int> remaining_costs = operator_costs;
    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
        auto pair = abstraction.compute_goal_distances_and_saturated_costs(
            remaining_costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        h_values_by_abstraction[pos] = move(h_values);
        reduce_costs(remaining_costs, saturated_costs);
    }
    return h_values_by_abstraction;
}

SaturatedCostPartitioningHeuristic::SaturatedCostPartitioningHeuristic(const Options &opts)
    : Heuristic(opts),
      abstraction_generators(
          opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators")) {
    vector<unique_ptr<Abstraction>> abstractions;
    for (const shared_ptr<AbstractionGenerator> &generator : abstraction_generators) {
        vector<unique_ptr<Abstraction>> current_abstractions =
            generator->generate_abstractions(task);
        move(current_abstractions.begin(), current_abstractions.end(), back_inserter(abstractions));
    }
    vector<int> order = get_default_order(abstractions.size());
    vector<vector<int>> h_values_by_abstraction = compute_saturated_cost_partitioning(
        abstractions, order, get_operator_costs(task_proxy));
    h_values_by_order.push_back(move(h_values_by_abstraction));
}

int SaturatedCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int SaturatedCostPartitioningHeuristic::compute_heuristic(const State &state) {
    (void) state;
    return 0;
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
