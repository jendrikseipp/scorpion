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
    const Abstractions &abstractions,
    int num_operators) {
    int num_abstractions = abstractions.size();

    vector<dynamic_bitset::DynamicBitset<>> relevant_operators;
    relevant_operators.reserve(num_abstractions);
    for (const auto &abstraction : abstractions) {
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
    const vector<int> operator_costs = task_properties::get_operator_costs(task_proxy);

    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"));
    cout << "Abstractions: " << abstractions.size() << endl;

    utils::Log() << "Compute abstract goal distances" << endl;
    for (const auto &abstraction : abstractions) {
        h_values_by_abstraction.push_back(
            abstraction->compute_goal_distances(operator_costs));
    }

    utils::Log() << "Compute max additive subsets" << endl;
    max_additive_subsets = compute_max_additive_subsets(
        abstractions, operator_costs.size());

    for (auto &abstraction : abstractions) {
        abstraction_functions.push_back(abstraction->extract_abstraction_function());
    }
}

int CanonicalHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int CanonicalHeuristic::compute_heuristic(const State &state) {
    vector<int> abstract_state_ids = get_abstract_state_ids(abstraction_functions, state);
    int max_h = compute_max_h(abstract_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int CanonicalHeuristic::compute_max_h(const vector<int> &abstract_state_ids) const {
    int max_h = 0;
    for (const MaxAdditiveSubset &additive_subset : max_additive_subsets) {
        int sum_h = 0;
        for (int abstraction_id : additive_subset) {
            int state_id = abstract_state_ids[abstraction_id];
            const vector<int> &h_values = h_values_by_abstraction[abstraction_id];
            int h = h_values[state_id];
            if (h == INF) {
                return INF;
            }
            sum_h += h;
        }
        max_h = max(max_h, sum_h);
    }
    assert(max_h >= 0);
    return max_h;
}


static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Canonical heuristic for abstraction heuristics",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return make_shared<CanonicalHeuristic>(opts);
}

static Plugin<Evaluator> _plugin("canonical_heuristic", _parse);
}
