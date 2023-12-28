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
#include "../task_utils/task_properties.h"
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


static pair<bool, unique_ptr<Abstraction>> convert_abstraction(
    cartesian_abstractions::Abstraction &cartesian_abstraction,
    const vector<int> &operator_costs) {
    // Compute h values.
    int initial_state_id = cartesian_abstraction.get_initial_state().get_id();
    vector<int> h_values = cartesian_abstractions::compute_goal_distances(
        cartesian_abstraction, operator_costs, cartesian_abstraction.get_goals());

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

    vector<int> goal_states(
        cartesian_abstraction.get_goals().begin(),
        cartesian_abstraction.get_goals().end());

    bool unsolvable = h_values[initial_state_id] == INF;
    return {
        unsolvable,
        utils::make_unique_ptr<ExplicitAbstraction>(
            utils::make_unique_ptr<CartesianAbstractionFunction>(
                cartesian_abstraction.extract_refinement_hierarchy()),
            move(backward_graph),
            cartesian_abstraction.get_looping_operators(),
            move(goal_states))
    };
}


CartesianAbstractionGenerator::CartesianAbstractionGenerator(
    const plugins::Options &opts)
    : AbstractionGenerator(opts),
      subtask_generators(
          opts.get_list<shared_ptr<cartesian_abstractions::SubtaskGenerator>>("subtasks")),
      max_states(opts.get<int>("max_states")),
      max_transitions(opts.get<int>("max_transitions")),
      max_time(opts.get<double>("max_time")),
      store_spt_children(opts.get<bool>("store_shortest_path_tree_children")),
      store_spt_parents(opts.get<bool>("store_shortest_path_tree_parents")),
      pick_flawed_abstract_state(
          opts.get<cartesian_abstractions::PickFlawedAbstractState>("pick_flawed_abstract_state")),
      pick_split(opts.get<cartesian_abstractions::PickSplit>("pick_split")),
      tiebreak_split(opts.get<cartesian_abstractions::PickSplit>("tiebreak_split")),
      max_concrete_states_per_abstract_state(
          opts.get<int>("max_concrete_states_per_abstract_state")),
      max_state_expansions(opts.get<int>("max_state_expansions")),
      extra_memory_padding_mb(opts.get<int>("memory_padding")),
      rng(utils::parse_rng_from_options(opts)),
      dot_graph_verbosity(opts.get<cartesian_abstractions::DotGraphVerbosity>("dot_graph_verbosity")),
      num_states(0),
      num_transitions(0) {
    cartesian_abstractions::g_hacked_extra_memory_padding_mb = opts.get<int>("memory_padding");
    cartesian_abstractions::g_hacked_tsr = opts.get<cartesian_abstractions::TransitionRepresentation>("transition_representation");
    cartesian_abstractions::g_hacked_sort_transitions = opts.get<bool>("sort_transitions");
    cartesian_abstractions::g_hacked_use_abstract_flaw_search = opts.get<bool>("use_abstract_flaw_search");
    cartesian_abstractions::g_hacked_shrink_spt_vectors_interval = opts.get<int>("shrink_spt_vectors_interval");
}

bool CartesianAbstractionGenerator::has_reached_resource_limit(
    const utils::CountdownTimer &timer) const {
    return num_states >= max_states ||
           num_transitions >= max_transitions ||
           timer.is_expired() ||
           !utils::extra_memory_padding_is_reserved();
}

unique_ptr<cartesian_abstractions::Abstraction> CartesianAbstractionGenerator::build_abstraction_for_subtask(
    const shared_ptr<AbstractTask> &subtask,
    int remaining_subtasks,
    const utils::CountdownTimer &timer) {
    cartesian_abstractions::CEGAR cegar(
        subtask,
        max(1, (max_states - num_states) / remaining_subtasks),
        max(1, (max_transitions - num_transitions) / remaining_subtasks),
        timer.get_remaining_time() / remaining_subtasks,
        pick_flawed_abstract_state,
        pick_split,
        tiebreak_split,
        max_concrete_states_per_abstract_state,
        max_state_expansions,
        store_spt_children,
        store_spt_parents,
        *rng,
        log,
        dot_graph_verbosity);
    cout << endl;
    return cegar.extract_abstraction();
}

void CartesianAbstractionGenerator::build_abstractions_for_subtasks(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const utils::CountdownTimer &timer,
    Abstractions &abstractions) {
    log << "Build abstractions for " << subtasks.size() << " subtasks in "
        << timer.get_remaining_time() << endl;
    int remaining_subtasks = subtasks.size();
    for (const shared_ptr<AbstractTask> &subtask : subtasks) {
        unique_ptr<cartesian_abstractions::Abstraction> cartesian_abstraction =
            build_abstraction_for_subtask(subtask, remaining_subtasks, timer);

        /* If we run out of memory while building an abstraction, we discard it
           to avoid running out of memory during the abstraction conversion. */
        if (!utils::extra_memory_padding_is_reserved()) {
            break;
        }

        num_states += cartesian_abstraction->get_num_states();

        vector<int> operator_costs = task_properties::get_operator_costs(TaskProxy(*subtask));
        auto [unsolvable, abstraction] = convert_abstraction(*cartesian_abstraction, operator_costs);
        // This is needlessly slow by looping over all transitions, but it's probably not worth optimizing this.
        abstraction->for_each_transition([this](const Transition &) {++num_transitions;});
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
};

static plugins::FeaturePlugin<CartesianAbstractionGeneratorFeature> _plugin;
}
