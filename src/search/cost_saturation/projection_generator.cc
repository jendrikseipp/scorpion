#include "projection_generator.h"

#include "abstraction.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../pdbs/pattern_generator.h"

#include <memory>

using namespace std;

namespace cost_saturation {
ProjectionGenerator::ProjectionGenerator(const options::Options &opts)
    : pattern_generator(
          opts.get<shared_ptr<pdbs::PatternCollectionGenerator>>("patterns")) {
}

unique_ptr<Abstraction> compute_abstraction(
    const TaskProxy &task_proxy, const pdbs::Pattern &pattern) {
    (void) pattern;
    vector<vector<Transition>> backward_graph;
    vector<int> looping_operators;
    vector<int> goal_states;
    return utils::make_unique_ptr<Abstraction>(
        move(backward_graph),
        move(looping_operators),
        move(goal_states),
        task_proxy.get_operators().size());
}

vector<unique_ptr<Abstraction>> ProjectionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task) {
    TaskProxy task_proxy(*task);
    vector<unique_ptr<Abstraction>> abstractions;
    shared_ptr<pdbs::PatternCollection> patterns =
        pattern_generator->generate(task).get_patterns();
    for (const pdbs::Pattern &pattern : *patterns) {
        abstractions.push_back(compute_abstraction(task_proxy, pattern));
    }
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

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<ProjectionGenerator>(opts);
}

static PluginShared<AbstractionGenerator> _plugin("projections", _parse);
}
