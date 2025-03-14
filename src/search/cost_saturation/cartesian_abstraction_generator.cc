#include "cartesian_abstraction_generator.h"

#include "explicit_abstraction.h"
#include "types.h"

#include "../cartesian_abstractions/abstraction.h"
#include "../cartesian_abstractions/abstract_state.h"
#include "../cartesian_abstractions/cegar.h"
#include "../cartesian_abstractions/cost_saturation.h"
#include "../cartesian_abstractions/refinement_hierarchy.h"
#include "../cartesian_abstractions/split_selector.h"
#include "../cartesian_abstractions/subtask_generators.h"
#include "../cartesian_abstractions/transition_system.h"
#include "../cartesian_abstractions/utils.h"
#include "../plugins/plugin.h"
#include "../utils/memory.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
class CartesianAbstractionFunction : public AbstractionFunction {
    unique_ptr<cartesian_abstractions::RefinementHierarchy> refinement_hierarchy;

public:
    explicit CartesianAbstractionFunction(
        unique_ptr<cartesian_abstractions::RefinementHierarchy> refinement_hierarchy)
        : refinement_hierarchy(move(refinement_hierarchy)) {
    }

    virtual int get_abstract_state_id(const State &concrete_state) const override {
        return refinement_hierarchy->get_abstract_state_id(concrete_state);
    }
};


static vector<vector<Successor>> get_backward_graph(
    const cartesian_abstractions::Abstraction &cartesian_abstraction,
    const vector<int> &h_values) {
    // Retrieve non-looping transitions.
    vector<vector<Successor>> backward_graph(cartesian_abstraction.get_num_states());
    for (int target = 0; target < cartesian_abstraction.get_num_states(); ++target) {
        // Prune transitions *to* unsolvable states.
        if (h_values[target] == INF) {
            continue;
        }
        for (const cartesian_abstractions::Transition &transition :
             cartesian_abstraction.get_incoming_transitions(target)) {
            int src = transition.target_id;
            // Prune transitions *from* unsolvable states.
            if (h_values[src] == INF) {
                continue;
            }
            backward_graph[target].emplace_back(transition.op_id, src);
        }
        backward_graph[target].shrink_to_fit();
    }
    return backward_graph;
}


CartesianAbstractionGenerator::CartesianAbstractionGenerator(
    const vector<shared_ptr<cartesian_abstractions::SubtaskGenerator>> &subtasks,
    int max_states, int max_transitions, double max_time,
    cartesian_abstractions::PickFlawedAbstractState pick_flawed_abstract_state,
    cartesian_abstractions::PickSplit pick_split,
    cartesian_abstractions::PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state, int max_state_expansions,
    cartesian_abstractions::TransitionRepresentation transition_representation,
    int memory_padding, int random_seed,
    cartesian_abstractions::DotGraphVerbosity dot_graph_verbosity,
    utils::Verbosity verbosity)
    : AbstractionGenerator(verbosity),
      subtask_generators(subtasks),
      max_states(max_states),
      max_transitions(max_transitions),
      max_time(max_time),
      transition_representation(transition_representation),
      pick_flawed_abstract_state(pick_flawed_abstract_state),
      pick_split(pick_split),
      tiebreak_split(tiebreak_split),
      max_concrete_states_per_abstract_state(max_concrete_states_per_abstract_state),
      max_state_expansions(max_state_expansions),
      extra_memory_padding_mb(memory_padding),
      rng(utils::get_rng(random_seed)),
      dot_graph_verbosity(dot_graph_verbosity),
      num_states(0),
      num_transitions(0) {
}

bool CartesianAbstractionGenerator::has_reached_resource_limit(
    const utils::CountdownTimer &timer) const {
    return num_states >= max_states ||
           num_transitions >= max_transitions ||
           timer.is_expired() ||
           !utils::extra_memory_padding_is_reserved();
}

void CartesianAbstractionGenerator::build_abstractions_for_subtasks(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const utils::CountdownTimer &timer,
    Abstractions &abstractions) {
    log << "Build abstractions for " << subtasks.size() << " subtasks in "
        << timer.get_remaining_time() << endl;
    int remaining_subtasks = subtasks.size();
    for (const shared_ptr<AbstractTask> &subtask : subtasks) {
        auto cegar = make_unique<cartesian_abstractions::CEGAR>(
            subtask,
            max(1, (max_states - num_states) / remaining_subtasks),
            max(1, (max_transitions - num_transitions) / remaining_subtasks),
            timer.get_remaining_time() / remaining_subtasks,
            pick_flawed_abstract_state,
            pick_split,
            tiebreak_split,
            max_concrete_states_per_abstract_state,
            max_state_expansions,
            transition_representation,
            *rng,
            log,
            dot_graph_verbosity);
        cout << endl;
        auto cartesian_abstraction = cegar->extract_abstraction();
        vector<int> goal_distances = cegar->get_goal_distances();
        cegar.release();  // Release memory for shortest paths, flaw search, etc.

        /* If we run out of memory while building an abstraction, we discard it
           to avoid running out of memory during the abstraction conversion. */
        if (!utils::extra_memory_padding_is_reserved()) {
            break;
        }

        num_states += cartesian_abstraction->get_num_states();
        int initial_state_id = cartesian_abstraction->get_initial_state().get_id();
        bool unsolvable = goal_distances[initial_state_id] == INF;

        auto backward_graph = get_backward_graph(*cartesian_abstraction, goal_distances);
        for (const auto &transitions : backward_graph) {
            num_transitions += transitions.size();
        }
        vector<int> goal_states(
            cartesian_abstraction->get_goals().begin(),
            cartesian_abstraction->get_goals().end());
        auto abstraction = make_unique<ExplicitAbstraction>(
            make_unique<CartesianAbstractionFunction>(
                cartesian_abstraction->extract_refinement_hierarchy()),
            move(backward_graph),
            cartesian_abstraction->get_looping_operators(),
            move(goal_states));
        abstractions.push_back(move(abstraction));

        if (has_reached_resource_limit(timer) || unsolvable) {
            break;
        }

        --remaining_subtasks;
    }
}

Abstractions CartesianAbstractionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task,
    DeadEnds *) {
    utils::CountdownTimer timer(max_time);
    num_states = 0;
    num_transitions = 0;
    log << "Build Cartesian abstractions" << endl << endl;

    // The CEGAR code expects that some extra memory is reserved.
    utils::reserve_extra_memory_padding(extra_memory_padding_mb);

    Abstractions abstractions;
    for (const auto &subtask_generator : subtask_generators) {
        cartesian_abstractions::SharedTasks subtasks = subtask_generator->get_subtasks(task, log);
        build_abstractions_for_subtasks(subtasks, timer, abstractions);
        if (has_reached_resource_limit(timer)) {
            break;
        }
    }

    if (utils::extra_memory_padding_is_reserved()) {
        utils::release_extra_memory_padding();
    }

    log << "Cartesian abstractions: " << abstractions.size() << endl;
    log << "Time for building Cartesian abstractions: "
        << timer.get_elapsed_time() << endl;
    log << "Total number of Cartesian states: " << num_states << endl;
    log << "Total number of transitions in Cartesian abstractions: "
        << num_transitions << endl;
    return abstractions;
}

class CartesianAbstractionGeneratorFeature
    : public plugins::TypedFeature<AbstractionGenerator, CartesianAbstractionGenerator> {
public:
    CartesianAbstractionGeneratorFeature() : TypedFeature("cartesian") {
        document_title("Cartesian abstraction generator");
        cartesian_abstractions::add_common_cegar_options(*this);
        utils::add_log_options_to_feature(*this);
    }

    virtual shared_ptr<CartesianAbstractionGenerator> create_component(
        const plugins::Options &opts) const override {
        cartesian_abstractions::g_hacked_sort_transitions = opts.get<bool>("sort_transitions");

        return plugins::make_shared_from_arg_tuples<CartesianAbstractionGenerator>(
            opts.get_list<shared_ptr<cartesian_abstractions::SubtaskGenerator>>("subtasks"),
            opts.get<int>("max_states"),
            opts.get<int>("max_transitions"),
            opts.get<double>("max_time"),
            opts.get<cartesian_abstractions::PickFlawedAbstractState>("pick_flawed_abstract_state"),
            opts.get<cartesian_abstractions::PickSplit>("pick_split"),
            opts.get<cartesian_abstractions::PickSplit>("tiebreak_split"),
            opts.get<int>("max_concrete_states_per_abstract_state"),
            opts.get<int>("max_state_expansions"),
            opts.get<cartesian_abstractions::TransitionRepresentation>("transition_representation"),
            opts.get<int>("memory_padding"),
            utils::get_rng_arguments_from_options(opts),
            opts.get<cartesian_abstractions::DotGraphVerbosity>("dot_graph_verbosity"),
            opts.get<utils::Verbosity>("verbosity"));
    }
};

static plugins::FeaturePlugin<CartesianAbstractionGeneratorFeature> _plugin;
}
