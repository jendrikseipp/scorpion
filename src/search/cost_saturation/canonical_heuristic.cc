#include "canonical_heuristic.h"

#include "abstraction.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../algorithms/max_cliques.h"
#include "../algorithms/dynamic_bitset.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
class AbstractionGenerator;

static MaxAdditiveSubsets compute_max_additive_subsets(
    const Abstractions &abstractions) {
    int num_abstractions = abstractions.size();

    vector<dynamic_bitset::DynamicBitset<>> relevant_operators;
    relevant_operators.reserve(num_abstractions);
    for (const auto &abstraction : abstractions) {
        int num_operators = abstraction->get_num_operators();
        dynamic_bitset::DynamicBitset<> active_ops(num_operators);
        for (int op_id = 0; op_id < num_operators; ++op_id) {
            if (abstraction->operator_is_active(op_id)) {
                active_ops.set(op_id);
            }
        }
        relevant_operators.push_back(move(active_ops));
    }

    // Initialize compatibility graph.
    vector<vector<int>> cgraph;
    cgraph.resize(num_abstractions);

    for (int i = 0; i < num_abstractions; ++i) {
        for (int j = i + 1; j < num_abstractions; ++j) {
            if (!relevant_operators[i].intersects(relevant_operators[j])) {
                /* If the two abstractions are additive, there is an edge in the
                   compatibility graph. */
                cgraph[i].push_back(j);
                cgraph[j].push_back(i);
            }
        }
    }

    MaxAdditiveSubsets max_cliques;
    max_cliques::compute_max_cliques(cgraph, max_cliques);
    return max_cliques;
}

CanonicalHeuristic::CanonicalHeuristic(const Options &opts)
    : Heuristic(opts) {
    vector<int> costs = task_properties::get_operator_costs(task_proxy);

    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"));

    utils::g_log << "Compute abstract goal distances" << endl;
    for (const auto &abstraction : abstractions) {
        h_values_by_abstraction.push_back(
            abstraction->compute_goal_distances(costs));
    }

    utils::g_log << "Compute max additive subsets" << endl;
    max_additive_subsets = compute_max_additive_subsets(abstractions);

    for (const auto &abstraction : abstractions) {
        abstraction_functions.push_back(abstraction->extract_abstraction_function());
    }
}

int CanonicalHeuristic::compute_heuristic(const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    vector<int> h_values_for_state;
    h_values_for_state.reserve(abstraction_functions.size());
    for (size_t i = 0; i < abstraction_functions.size(); ++i) {
        int state_id = abstraction_functions[i]->get_abstract_state_id(state);
        int h = h_values_by_abstraction[i][state_id];
        if (h == INF) {
            return DEAD_END;
        }
        h_values_for_state.push_back(h);
    }
    return compute_max_over_sums(h_values_for_state);
}

int CanonicalHeuristic::compute_max_over_sums(
    const vector<int> &h_values_for_state) const {
    int max_h = 0;
    for (const MaxAdditiveSubset &additive_subset : max_additive_subsets) {
        int sum_h = 0;
        for (int abstraction_id : additive_subset) {
            int h = h_values_for_state[abstraction_id];
            assert(h != INF);
            sum_h += h;
            assert(sum_h >= 0);
        }
        max_h = max(max_h, sum_h);
    }
    return max_h;
}


static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Canonical heuristic over abstractions",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return make_shared<CanonicalHeuristic>(opts);
}

static Plugin<Evaluator> _plugin(
    "canonical_heuristic", _parse, "heuristics_cost_partitioning");
}
