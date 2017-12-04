#include "cartesian_abstraction_generator.h"

#include "explicit_abstraction.h"
#include "types.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../cegar/abstraction.h"
#include "../cegar/abstract_state.h"
#include "../cegar/cost_saturation.h"
#include "../utils/logging.h"
#include "../utils/rng_options.h"

#include <memory>

using namespace std;

namespace cost_saturation {
CartesianAbstractionGenerator::CartesianAbstractionGenerator(
    const options::Options &opts)
    : subtask_generators(
          opts.get_list<shared_ptr<cegar::SubtaskGenerator>>("subtasks")),
      max_transitions(opts.get<int>("max_transitions")),
      rng(utils::parse_rng_from_options(opts)) {
}

static unique_ptr<Abstraction> convert_abstraction(
    const TaskProxy &task_proxy,
    const cegar::Abstraction &cartesian_abstraction) {
    int num_states = cartesian_abstraction.get_num_states();
    vector<vector<Transition>> backward_graph(num_states);

    // Store non-looping transitions.
    for (cegar::AbstractState *state : cartesian_abstraction.get_states()) {
        // Ignore transitions from dead-end or unreachable states.
        if (state->get_h_value() == INF ||
            state->get_search_info().get_g_value() == INF) {
            continue;
        }
        int src = state->get_node()->get_state_id();
        for (const cegar::Transition &transition : state->get_outgoing_transitions()) {
            // Ignore transitions from dead-end states (we know target is reachable).
            if (transition.target->get_h_value() == INF) {
                continue;
            }
            int target = transition.target->get_node()->get_state_id();
            backward_graph[target].emplace_back(transition.op_id, src);
        }
    }

    // Store self-loop info.
    vector<int> looping_operators;
    const vector<bool> &self_loop_info =
        cartesian_abstraction.get_operator_induces_self_loop();
    for (size_t op_id = 0; op_id < self_loop_info.size(); ++op_id) {
        if (self_loop_info[op_id]) {
            looping_operators.push_back(op_id);
        }
    }

    // Store goals.
    vector<int> goal_states;
    for (const cegar::AbstractState *goal : cartesian_abstraction.get_goals()) {
        goal_states.push_back(goal->get_node()->get_state_id());
    }

    shared_ptr<cegar::RefinementHierarchy> refinement_hierarchy =
        cartesian_abstraction.get_refinement_hierarchy();
    AbstractionFunction state_map =
        [refinement_hierarchy](const State &state) {
            assert(refinement_hierarchy);
            return refinement_hierarchy->get_local_state_id(state);
        };

    return utils::make_unique_ptr<ExplicitAbstraction>(
        state_map,
        move(backward_graph),
        move(looping_operators),
        move(goal_states),
        task_proxy.get_operators().size());
}

Abstractions CartesianAbstractionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task) {
    Abstractions abstractions;

    utils::Timer timer;
    utils::Log log;
    TaskProxy task_proxy(*task);

    log << "Generate CEGAR abstractions" << endl;

    const int max_states = INF;
    const double max_time = numeric_limits<double>::infinity();
    const bool use_general_costs = true;
    const bool exclude_abstractions_with_zero_init_h = true;
    cegar::CostSaturation cost_saturation(
        cegar::CostPartitioningType::SATURATED_POSTHOC,
        subtask_generators,
        max_states,
        max_transitions,
        max_time,
        use_general_costs,
        exclude_abstractions_with_zero_init_h,
        cegar::PickSplit::MAX_REFINED,
        *rng);
    cost_saturation.initialize(task);

    vector<unique_ptr<cegar::Abstraction>> cartesian_abstractions =
        cost_saturation.extract_abstractions();

    cout << "Cartesian abstractions: " << cartesian_abstractions.size() << endl;

    log << "Convert to backward-graph abstractions" << endl;
    for (auto &cartesian_abstraction : cartesian_abstractions) {
        abstractions.push_back(
            convert_abstraction(task_proxy, *cartesian_abstraction));
        cartesian_abstraction = nullptr;
    }
    log << "Done converting abstractions" << endl;
    cout << "Time for building Cartesian abstractions: " << timer << endl;
    return abstractions;
}

static shared_ptr<AbstractionGenerator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Cartesian abstraction generator",
        "");

    parser.add_list_option<shared_ptr<cegar::SubtaskGenerator>>(
        "subtasks",
        "subtask generators",
        "[landmarks(order=random, random_seed=0),goals(order=random, random_seed=0)]");
    parser.add_option<int>(
        "max_transitions",
        "maximum sum of real transitions (excluding self-loops) over "
        " all abstractions",
        "1000000",
        Bounds("0", "infinity"));
    parser.add_option<bool>(
        "debug",
        "print debugging info",
        "false");
    utils::add_rng_options(parser);

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<CartesianAbstractionGenerator>(opts);
}

static PluginShared<AbstractionGenerator> _plugin("cartesian", _parse);
}
