#include "additive_cartesian_heuristic.h"

#include "cartesian_heuristic_function.h"
#include "cost_saturation.h"
#include "types.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/rng_options.h"

#include <cassert>

using namespace std;

namespace cartesian_abstractions {
AdditiveCartesianHeuristic::AdditiveCartesianHeuristic(
    const vector<shared_ptr<SubtaskGenerator>> &subtasks,
    int max_states, int max_transitions, double max_time,
    PickFlawedAbstractState pick_flawed_abstract_state,
    PickSplit pick_split, PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state, int max_state_expansions,
    TransitionRepresentation transition_representation,
    bool store_shortest_path_tree_children, bool store_shortest_path_tree_parents,
    int memory_padding, bool use_max, int random_seed, DotGraphVerbosity dot_graph_verbosity,
    bool use_general_costs, const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description, utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      use_max(use_max) {
    CostSaturation cost_saturation(
        subtasks,
        max_states,
        max_transitions,
        max_time,
        use_general_costs,
        pick_flawed_abstract_state,
        pick_split,
        tiebreak_split,
        max_concrete_states_per_abstract_state,
        max_state_expansions,
        transition_representation,
        store_shortest_path_tree_children,
        store_shortest_path_tree_parents,
        memory_padding,
        use_max,
        *utils::get_rng(random_seed),
        log,
        dot_graph_verbosity);
    heuristic_functions = cost_saturation.generate_heuristic_functions(transform);
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

class AdditiveCartesianHeuristicFeature
    : public plugins::TypedFeature<Evaluator, AdditiveCartesianHeuristic> {
public:
    AdditiveCartesianHeuristicFeature() : TypedFeature("cegar") {
        document_title("Additive Cartesian CEGAR heuristic");
        document_synopsis(
            "See the paper introducing counterexample-guided Cartesian "
            "abstraction refinement (CEGAR) for classical planning:" +
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

        add_common_cegar_options(*this);
        add_option<bool>(
            "use_general_costs",
            "allow negative costs in cost partitioning",
            "true");
        add_heuristic_options_to_feature(*this, "cegar");

        document_language_support("action costs", "supported");
        document_language_support("conditional effects", "not supported");
        document_language_support("axioms", "not supported");

        document_property("admissible", "yes");
        document_property("consistent", "yes");
        document_property("safe", "yes");
        document_property("preferred operators", "no");
    }

    virtual shared_ptr<AdditiveCartesianHeuristic> create_component(
        const plugins::Options &opts,
        const utils::Context &) const override {
        g_hacked_sort_transitions = opts.get<bool>("sort_transitions");
        g_hacked_use_abstract_flaw_search = opts.get<bool>("use_abstract_flaw_search");
        return plugins::make_shared_from_arg_tuples<AdditiveCartesianHeuristic>(
            opts.get_list<shared_ptr<SubtaskGenerator>>("subtasks"),
            opts.get<int>("max_states"),
            opts.get<int>("max_transitions"),
            opts.get<double>("max_time"),
            opts.get<PickFlawedAbstractState>("pick_flawed_abstract_state"),
            opts.get<PickSplit>("pick_split"),
            opts.get<PickSplit>("tiebreak_split"),
            opts.get<int>("max_concrete_states_per_abstract_state"),
            opts.get<int>("max_state_expansions"),
            opts.get<TransitionRepresentation>("transition_representation"),
            opts.get<bool>("store_shortest_path_tree_children"),
            opts.get<bool>("store_shortest_path_tree_parents"),
            opts.get<int>("memory_padding"),
            opts.get<bool>("use_max"),
            utils::get_rng_arguments_from_options(opts),
            opts.get<DotGraphVerbosity>("dot_graph_verbosity"),
            opts.get<bool>("use_general_costs"),
            get_heuristic_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<AdditiveCartesianHeuristicFeature> _plugin;
}
