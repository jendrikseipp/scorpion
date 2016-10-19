#include "projection_generator.h"

#include "abstraction.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../merge_and_shrink/factored_transition_system.h"
#include "../merge_and_shrink/fts_factory.h"
#include "../merge_and_shrink/heuristic_representation.h"
#include "../merge_and_shrink/label_equivalence_relation.h"
#include "../merge_and_shrink/transition_system.h"
#include "../merge_and_shrink/types.h"
#include "../pdbs/pattern_generator.h"
#include "../algorithms/ordered_set.h"
#include "../utils/logging.h"

#include <memory>

using namespace std;

namespace cost_saturation {
ProjectionGenerator::ProjectionGenerator(const options::Options &opts)
    : pattern_generator(
          opts.get<shared_ptr<pdbs::PatternCollectionGenerator>>("patterns")),
      debug(opts.get<bool>("debug")) {
}

pair<unique_ptr<Abstraction>, function<int (const State &)>> compute_abstraction(
    const TaskProxy &task_proxy, const pdbs::Pattern &pattern, bool debug) {
    const merge_and_shrink::Verbosity verbosity = debug ?
        merge_and_shrink::Verbosity::VERBOSE :
        merge_and_shrink::Verbosity::NORMAL;
    const bool compute_label_equivalence_relation = false;
    merge_and_shrink::FactoredTransitionSystem fts =
        merge_and_shrink::create_factored_transition_system(
            task_proxy, compute_label_equivalence_relation, verbosity);
    vector<int> unmerged_indices = pattern;
    assert(!unmerged_indices.empty());
    assert(utils::is_sorted_unique(unmerged_indices));
    reverse(unmerged_indices.begin(), unmerged_indices.end());
    while (unmerged_indices.size() > 1) {
        int index1 = unmerged_indices.back();
        unmerged_indices.pop_back();
        int index2 = unmerged_indices.back();
        unmerged_indices.pop_back();
        int new_index = fts.merge(index1, index2, verbosity);
        unmerged_indices.push_back(new_index);
    }
    assert(unmerged_indices.size() == 1);
    int final_index = unmerged_indices[0];
    const merge_and_shrink::TransitionSystem &transition_system =
        fts.get_ts(final_index);
    if (debug) {
        transition_system.dump_labels_and_transitions();
        transition_system.dump_dot_graph();
    }

    int num_states = transition_system.get_size();
    vector<vector<Transition>> backward_graph(num_states);
    algorithms::OrderedSet<int> looping_operators;
    for (const merge_and_shrink::GroupAndTransitions &gat : transition_system) {
        const merge_and_shrink::LabelGroup &label_group = gat.label_group;
        for (int op_id : label_group) {
            for (const merge_and_shrink::Transition &transition : gat.transitions) {
                if (transition.src == transition.target) {
                    looping_operators.insert(op_id);
                } else {
                    backward_graph[transition.target].emplace_back(op_id, transition.src);
                }
            }
        }
    }

    vector<int> goal_states;
    for (int state = 0; state < num_states; ++state) {
        if (transition_system.is_goal_state(state)) {
            goal_states.push_back(state);
        }
    }

    shared_ptr<merge_and_shrink::HeuristicRepresentation> heuristic_representation =
        fts.get_heuristic_representation(final_index);
    function<int (const State &)> lookup_function =
        [&heuristic_representation](const State &state) {
            return heuristic_representation->get_abstract_state(state);
        };

    return make_pair(utils::make_unique_ptr<Abstraction>(
        move(backward_graph),
        looping_operators.pop_as_vector(),
        move(goal_states),
        task_proxy.get_operators().size()), lookup_function);
}

vector<unique_ptr<Abstraction>> ProjectionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task) {
    utils::Timer timer;
    utils::Log log;
    log << "Building projections" << endl;
    TaskProxy task_proxy(*task);
    vector<unique_ptr<Abstraction>> abstractions;
    shared_ptr<pdbs::PatternCollection> patterns =
        pattern_generator->generate(task).get_patterns();
    cout << "Patterns: " << patterns->size() << endl;
    for (const pdbs::Pattern &pattern : *patterns) {
        abstractions.push_back(compute_abstraction(task_proxy, pattern, debug).first);
    }
    log << "Done building projections" << endl;
    cout << "Time for building projections: " << timer << endl;
    return abstractions;
}

static shared_ptr<AbstractionGenerator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "ProjectionGenerator abstraction generator",
        "");

    parser.add_option<shared_ptr<pdbs::PatternCollectionGenerator>>(
        "patterns",
        "pattern generation method",
        "systematic(1)");
    parser.add_option<bool>(
        "debug",
        "print debugging info",
        "false");

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<ProjectionGenerator>(opts);
}

static PluginShared<AbstractionGenerator> _plugin("projections", _parse);
}
