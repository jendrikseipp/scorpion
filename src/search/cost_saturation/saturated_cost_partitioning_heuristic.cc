#include "saturated_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace cost_saturation {
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
