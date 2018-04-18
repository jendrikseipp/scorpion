#include "max_heuristic.h"

#include "abstraction.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"

using namespace std;

namespace cost_saturation {
MaxHeuristic::MaxHeuristic(const Options &opts, Abstractions &&abstractions)
    : Heuristic(opts),
      abstractions(move(abstractions)) {
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    for (auto &abstraction : this->abstractions) {
        h_values_by_abstraction.push_back(abstraction->compute_h_values(costs));
        abstraction->release_transition_system_memory();
    }
}

int MaxHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    int max_h = 0;
    for (size_t i = 0; i < abstractions.size(); ++i) {
        assert(utils::in_bounds(i, abstractions));
        int local_state_id = abstractions[i]->get_abstract_state_id(state);
        assert(utils::in_bounds(i, h_values_by_abstraction));
        assert(utils::in_bounds(local_state_id, h_values_by_abstraction[i]));
        int h = h_values_by_abstraction[i][local_state_id];
        assert(h >= 0);
        max_h = max(max_h, h);
    }
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Max heuristic",
        "Maximize over a set of abstraction heuristics");

    prepare_parser_for_cost_partitioning_heuristic(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    Abstractions abstractions = generate_abstractions(
        opts.get<shared_ptr<AbstractTask>>("transform"),
        opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"));

    return new MaxHeuristic(opts, move(abstractions));
}

static Plugin<Heuristic> _plugin("maximize", _parse);
}
