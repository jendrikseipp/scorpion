#include "additive_cartesian_heuristic.h"

#include "cartesian_heuristic_function.h"
#include "cost_saturation.h"
#include "types.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <cassert>

using namespace std;

namespace cegar {
static vector<CartesianHeuristicFunction> generate_heuristic_functions(
    const options::Options &opts, utils::LogProxy &log) {
    if (log.is_at_least_normal()) {
        log << "Initializing additive Cartesian heuristic..." << endl;
    }
    vector<shared_ptr<SubtaskGenerator>> subtask_generators =
        opts.get_list<shared_ptr<SubtaskGenerator>>("subtasks");
    shared_ptr<utils::RandomNumberGenerator> rng =
        utils::parse_rng_from_options(opts);
    CostSaturation cost_saturation(
        subtask_generators,
        opts.get<int>("max_states"),
        opts.get<int>("max_transitions"),
        opts.get<double>("max_time"),
        opts.get<bool>("use_general_costs"),
        opts.get<PickFlawedAbstractState>("pick_flawed_abstract_state"),
        opts.get<PickSplit>("pick_split"),
        opts.get<PickSplit>("tiebreak_split"),
        opts.get<int>("max_concrete_states_per_abstract_state"),
        opts.get<int>("max_state_expansions"),
        opts.get<SearchStrategy>("search_strategy"),
        opts.get<int>("memory_padding"),
        *rng,
        log,
        opts.get<DotGraphVerbosity>("dot_graph_verbosity"));
    return cost_saturation.generate_heuristic_functions(
        opts.get<shared_ptr<AbstractTask>>("transform"));
}

AdditiveCartesianHeuristic::AdditiveCartesianHeuristic(
    const options::Options &opts)
    : Heuristic(opts),
      heuristic_functions(generate_heuristic_functions(opts, log)) {
}

int AdditiveCartesianHeuristic::compute_heuristic(const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    int sum_h = 0;
    for (const CartesianHeuristicFunction &function : heuristic_functions) {
        int value = function.get_value(state);
        assert(value >= 0);
        if (value == INF)
            return DEAD_END;
        sum_h += value;
    }
    assert(sum_h >= 0);
    return sum_h;
}

static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Additive CEGAR heuristic",
        "See the paper introducing Counterexample-guided Abstraction "
        "Refinement (CEGAR) for classical planning:" +
        utils::format_conference_reference(
            {"Jendrik Seipp", "Malte Helmert"},
            "Counterexample-guided Cartesian Abstraction Refinement",
            "https://ai.dmi.unibas.ch/papers/seipp-helmert-icaps2013.pdf",
            "Proceedings of the 23rd International Conference on Automated "
            "Planning and Scheduling (ICAPS 2013)",
            "347-351",
            "AAAI Press",
            "2013") +
        "and the paper showing how to make the abstractions additive:" +
        utils::format_conference_reference(
            {"Jendrik Seipp", "Malte Helmert"},
            "Diverse and Additive Cartesian Abstraction Heuristics",
            "https://ai.dmi.unibas.ch/papers/seipp-helmert-icaps2014.pdf",
            "Proceedings of the 24th International Conference on "
            "Automated Planning and Scheduling (ICAPS 2014)",
            "289-297",
            "AAAI Press",
            "2014") +
        "For more details on Cartesian CEGAR and saturated cost partitioning, "
        "see the journal paper" +
        utils::format_journal_reference(
            {"Jendrik Seipp", "Malte Helmert"},
            "Counterexample-Guided Cartesian Abstraction Refinement for "
            "Classical Planning",
            "https://ai.dmi.unibas.ch/papers/seipp-helmert-jair2018.pdf",
            "Journal of Artificial Intelligence Research",
            "62",
            "535-577",
            "2018") +
        "For a description of the incremental search, see the paper" +
        utils::format_conference_reference(
            {"Jendrik Seipp", "Samuel von Allmen", "Malte Helmert"},
            "Incremental Search for Counterexample-Guided Cartesian Abstraction Refinement",
            "https://ai.dmi.unibas.ch/papers/seipp-et-al-icaps2020.pdf",
            "Proceedings of the 30th International Conference on "
            "Automated Planning and Scheduling (ICAPS 2020)",
            "244-248",
            "AAAI Press",
            "2020") +
        "Finally, we describe advanced flaw selection strategies here:" +
        utils::format_conference_reference(
            {"David Speck", "Jendrik Seipp"},
            "New Refinement Strategies for Cartesian Abstractions",
            "https://jendrikseipp.com/papers/speck-seipp-icaps2022.pdf",
            "Proceedings of the 32nd International Conference on "
            "Automated Planning and Scheduling (ICAPS 2022)",
            "to appear",
            "AAAI Press",
            "2022"));
    parser.document_language_support("action costs", "supported");
    parser.document_language_support("conditional effects", "not supported");
    parser.document_language_support("axioms", "not supported");
    parser.document_property("admissible", "yes");
    parser.document_property("consistent", "yes");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    add_common_cegar_options(parser);
    parser.add_option<bool>(
        "use_general_costs",
        "allow negative costs in cost partitioning",
        "true");
    Heuristic::add_options_to_parser(parser);

    Options opts = parser.parse();

    if (parser.dry_run())
        return nullptr;

    return make_shared<AdditiveCartesianHeuristic>(opts);
}

static Plugin<Evaluator> _plugin("cegar", _parse);
}
