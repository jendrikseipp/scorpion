#include "cartesian_abstraction_generator.h"

#include "explicit_abstraction.h"
#include "types.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../cegar/abstraction.h"
#include "../cegar/abstract_search.h"
#include "../cegar/abstract_state.h"
#include "../cegar/cegar.h"
#include "../cegar/cost_saturation.h"
#include "../cegar/refinement_hierarchy.h"
#include "../cegar/split_selector.h"
#include "../cegar/subtask_generators.h"
#include "../cegar/transition_system.h"
#include "../cegar/utils.h"
#include "../task_utils/task_properties.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
class CartesianAbstractionFunction : public AbstractionFunction {
    unique_ptr<cegar::RefinementHierarchy> refinement_hierarchy;

public:
    explicit CartesianAbstractionFunction(
        unique_ptr<cegar::RefinementHierarchy> refinement_hierarchy)
        : refinement_hierarchy(move(refinement_hierarchy)) {
    }

    virtual int get_abstract_state_id(const State &concrete_state) const override {
        return refinement_hierarchy->get_abstract_state_id(concrete_state);
    }
};


static vector<bool> get_looping_operators(
    const cegar::TransitionSystem &ts, const vector<int> &h_values) {
    assert(ts.get_loops().size() == h_values.size());
    int num_states = h_values.size();
    int num_operators = ts.get_num_operators();
    vector<bool> operator_induces_self_loop(num_operators, false);
    for (int state = 0; state < num_states; ++state) {
        // Ignore self-loops at unsolvable states.
        if (h_values[state] != INF) {
            for (int op_id : ts.get_loops()[state]) {
                operator_induces_self_loop[op_id] = true;
            }
        }
    }
    return operator_induces_self_loop;
}


static pair<bool, unique_ptr<Abstraction>> convert_abstraction(
    cegar::Abstraction &cartesian_abstraction,
    const vector<int> &operator_costs) {
    // Compute h values.
    const cegar::TransitionSystem &ts =
        cartesian_abstraction.get_transition_system();
    int initial_state_id = cartesian_abstraction.get_initial_state().get_id();
    vector<int> h_values = cegar::compute_distances(
        ts.get_incoming_transitions(), operator_costs, cartesian_abstraction.get_goals());

    // Retrieve non-looping transitions.
    vector<vector<Successor>> backward_graph(cartesian_abstraction.get_num_states());
    for (int target = 0; target < cartesian_abstraction.get_num_states(); ++target) {
        // Prune transitions *to* unsolvable states.
        if (h_values[target] == INF) {
            continue;
        }
        for (const cegar::Transition &transition : ts.get_incoming_transitions()[target]) {
            int src = transition.target_id;
            // Prune transitions *from* unsolvable states.
            if (h_values[src] == INF) {
                continue;
            }
            backward_graph[target].emplace_back(transition.op_id, src);
        }
        backward_graph[target].shrink_to_fit();
    }

    vector<bool> looping_operators = get_looping_operators(ts, h_values);
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
                   move(looping_operators),
                   move(goal_states))
    };
}


CartesianAbstractionGenerator::CartesianAbstractionGenerator(
    const options::Options &opts)
    : AbstractionGenerator(opts),
      subtask_generators(
          opts.get_list<shared_ptr<cegar::SubtaskGenerator>>("subtasks")),
      max_states(opts.get<int>("max_states")),
      max_transitions(opts.get<int>("max_transitions")),
      max_time(opts.get<double>("max_time")),
      search_strategy(opts.get<cegar::SearchStrategy>("search_strategy")),
      pick_flawed_abstract_state(
          opts.get<cegar::PickFlawedAbstractState>("pick_flawed_abstract_state")),
      pick_split(opts.get<cegar::PickSplit>("pick_split")),
      tiebreak_split(opts.get<cegar::PickSplit>("tiebreak_split")),
      max_concrete_states_per_abstract_state(
          opts.get<int>("max_concrete_states_per_abstract_state")),
      max_state_expansions(opts.get<int>("max_state_expansions")),
      extra_memory_padding_mb(opts.get<int>("memory_padding")),
      rng(utils::parse_rng_from_options(opts)),
      dot_graph_verbosity(opts.get<cegar::DotGraphVerbosity>("dot_graph_verbosity")),
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

unique_ptr<cegar::Abstraction> CartesianAbstractionGenerator::build_abstraction_for_subtask(
    const shared_ptr<AbstractTask> &subtask,
    int remaining_subtasks,
    const utils::CountdownTimer &timer) {
    cegar::CEGAR cegar(
        subtask,
        max(1, (max_states - num_states) / remaining_subtasks),
        max(1, (max_transitions - num_transitions) / remaining_subtasks),
        timer.get_remaining_time() / remaining_subtasks,
        pick_flawed_abstract_state,
        pick_split,
        tiebreak_split,
        max_concrete_states_per_abstract_state,
        max_state_expansions,
        search_strategy,
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
    int remaining_subtasks = subtasks.size();
    for (const shared_ptr<AbstractTask> &subtask : subtasks) {
        unique_ptr<cegar::Abstraction> cartesian_abstraction =
            build_abstraction_for_subtask(subtask, remaining_subtasks, timer);

        /* If we run out of memory while building an abstraction, we discard it
           to avoid running out of memory during the abstraction conversion. */
        if (!utils::extra_memory_padding_is_reserved()) {
            break;
        }

        num_states += cartesian_abstraction->get_num_states();
        num_transitions += cartesian_abstraction->get_transition_system().get_num_non_loops();

        vector<int> operator_costs = task_properties::get_operator_costs(TaskProxy(*subtask));
        auto result = convert_abstraction(*cartesian_abstraction, operator_costs);
        bool unsolvable = result.first;
        abstractions.push_back(move(result.second));

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
        cegar::SharedTasks subtasks = subtask_generator->get_subtasks(task, log);
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

static shared_ptr<AbstractionGenerator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Cartesian abstraction generator",
        "");

    cegar::add_common_cegar_options(parser);
    utils::add_log_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<CartesianAbstractionGenerator>(opts);
}

static Plugin<AbstractionGenerator> _plugin("cartesian", _parse);
}
