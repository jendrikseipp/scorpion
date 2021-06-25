#include "pattern_collection_generator_single_cegar.h"

#include "cegar.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/rng_options.h"

using namespace std;

namespace pdbs {
PatternCollectionGeneratorSingleCegar::PatternCollectionGeneratorSingleCegar(
    const options::Options &opts)
    : max_pdb_size(opts.get<int>("max_pdb_size")),
      max_collection_size(opts.get<int>("max_collection_size")),
      max_time(opts.get<double>("max_time")),
      use_wildcard_plans(opts.get<bool>("use_wildcard_plans")),
      verbosity(opts.get<utils::Verbosity>("verbosity")),
      rng(utils::parse_rng_from_options(opts)) {
}

PatternCollectionInformation PatternCollectionGeneratorSingleCegar::generate(
    const shared_ptr<AbstractTask> &task) {
    if (verbosity >= utils::Verbosity::NORMAL) {
        utils::g_log << "Generating patterns using the Single CEGAR algorithm."
                     << endl;
    }

    // Store the set of goals in random order.
    TaskProxy task_proxy(*task);
    vector<FactPair> goals = get_goals_in_random_order(task_proxy, *rng);

    CEGAR cegar(
        max_pdb_size,
        max_collection_size,
        max_time,
        use_wildcard_plans,
        verbosity,
        rng,
        task,
        move(goals));
    return cegar.compute_pattern_collection();
}

static shared_ptr<PatternCollectionGenerator> _parse(
    options::OptionParser &parser) {
    parser.document_synopsis(
        "Single CEGAR",
        "This pattern collection generator implements the single CEGAR algorithm "
        "described in the paper" + get_rovner_et_al_reference());
    add_implementation_notes_to_parser(parser);
    // TODO: these options could be move to the base class; see issue1022.
    parser.add_option<int>(
        "max_pdb_size",
        "maximum number of states per pattern database (ignored for the "
        "initial collection consisting of a singleton pattern for each goal "
        "variable)",
        "1000000",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_collection_size",
        "maximum number of states in the pattern collection (ignored for the "
        "initial collection consisting of a singleton pattern for each goal "
        "variable)",
        "10000000",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for this pattern collection generator "
        "(ignored for computing the initial collection consisting of a "
        "singleton pattern for each goal variable)",
        "infinity",
        Bounds("0.0", "infinity"));
    add_cegar_wildcard_option_to_parser(parser);
    utils::add_verbosity_option_to_parser(parser);
    utils::add_rng_options(parser);

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<PatternCollectionGeneratorSingleCegar>(opts);
}

static Plugin<PatternCollectionGenerator> _plugin("single_cegar", _parse);
}
