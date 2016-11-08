#include "projection_generator.h"

#include "projection.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../pdbs/pattern_generator.h"
#include "../utils/logging.h"

#include <memory>

using namespace pdbs;
using namespace std;

namespace cost_saturation {
ProjectionGenerator::ProjectionGenerator(const options::Options &opts)
    : pattern_generator(
          opts.get<shared_ptr<pdbs::PatternCollectionGenerator>>("patterns")),
      debug(opts.get<bool>("debug")) {
}

Abstractions ProjectionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task) {
    utils::Timer timer;
    utils::Log log;
    TaskProxy task_proxy(*task);

    log << "Compute patterns" << endl;
    shared_ptr<pdbs::PatternCollection> patterns =
        pattern_generator->generate(task).get_patterns();
    cout << "Patterns: " << patterns->size() << endl;

    log << "Build projections" << endl;
    Abstractions abstractions;
    for (const pdbs::Pattern &pattern : *patterns) {
        if (debug) {
            log << "Pattern: " << pattern << endl;
        }
        abstractions.push_back(
            utils::make_unique_ptr<Projection>(task_proxy, pattern));
    }
    log << "Done building projections" << endl;
    cout << "Time for building projections: " << timer << endl;
    return abstractions;
}

static shared_ptr<AbstractionGenerator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Projection generator",
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
