#include "max_heuristic.h"

#include "abstraction.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"

using namespace std;

namespace cost_saturation {
MaxHeuristic::MaxHeuristic(const Options &opts, const Abstractions &abstractions)
    : Heuristic(opts) {
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    for (auto &abstraction : abstractions) {
        h_values_by_abstraction.push_back(abstraction->compute_goal_distances(costs));
        abstraction_functions.push_back(abstraction->extract_abstraction_function());
    }
}

int MaxHeuristic::compute_heuristic(const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    int max_h = 0;
    for (size_t i = 0; i < abstraction_functions.size(); ++i) {
        int local_state_id = abstraction_functions[i]->get_abstract_state_id(state);
        int h = h_values_by_abstraction[i][local_state_id];
        assert(h >= 0);
        if (h == INF) {
            return DEAD_END;
        }
        max_h = max(max_h, h);
    }
    return max_h;
}


static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Maximum over abstractions",
        "Maximize over a set of abstraction heuristics.");

    prepare_parser_for_cost_partitioning_heuristic(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    Abstractions abstractions = generate_abstractions(
        opts.get<shared_ptr<AbstractTask>>("transform"),
        opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"));

    return make_shared<MaxHeuristic>(opts, move(abstractions));
}

static Plugin<Evaluator> _plugin("maximize", _parse, "heuristics_cost_partitioning");
}
