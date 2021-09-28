#include "cost_saturation.h"

#include "abstract_search.h"
#include "abstract_state.h"
#include "abstraction.h"
#include "cartesian_heuristic_function.h"
#include "cegar.h"
#include "refinement_hierarchy.h"
#include "subtask_generators.h"
#include "transition.h"
#include "transition_system.h"
#include "utils.h"

#include "../task_utils/task_properties.h"
#include "../tasks/modified_operator_costs_task.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace cegar {
static vector<int> compute_saturated_costs(
    const TransitionSystem &transition_system,
    const vector<int> &g_values,
    const vector<int> &h_values,
    bool use_general_costs) {
    const int min_cost = use_general_costs ? -INF : 0;
    vector<int> saturated_costs(transition_system.get_num_operators(), min_cost);
    assert(g_values.size() == h_values.size());
    int num_states = h_values.size();
    for (int state_id = 0; state_id < num_states; ++state_id) {
        int g = g_values[state_id];
        int h = h_values[state_id];

        /*
          No need to maintain goal distances of unreachable (g == INF)
          and dead end states (h == INF).

          Note that the "succ_h == INF" test below is sufficient for
          ignoring dead end states. The "h == INF" test is a speed
          optimization.
        */
        if (g == INF || h == INF)
            continue;

        for (const Transition &transition:
             transition_system.get_outgoing_transitions()[state_id]) {
            int op_id = transition.op_id;
            int succ_id = transition.target_id;
            int succ_h = h_values[succ_id];

            if (succ_h == INF)
                continue;

            int needed = h - succ_h;
            saturated_costs[op_id] = max(saturated_costs[op_id], needed);
        }

        if (use_general_costs) {
            /* To prevent negative cost cycles, all operators inducing
               self-loops must have non-negative costs. */
            for (int op_id : transition_system.get_loops()[state_id]) {
                saturated_costs[op_id] = max(saturated_costs[op_id], 0);
            }
        }
    }
    return saturated_costs;
}


CostSaturation::CostSaturation(
    const vector<shared_ptr<SubtaskGenerator>> &subtask_generators,
    int max_states,
    int max_non_looping_transitions,
    double max_time,
    bool use_general_costs,
    PickSplit pick_split,
    SearchStrategy search_strategy,
    FlawStrategy flaw_strategy,
    int memory_padding_mb,
    utils::RandomNumberGenerator &rng,
    bool debug,
    int dot_graph_verbosity)
    : subtask_generators(subtask_generators),
      max_states(max_states),
      max_non_looping_transitions(max_non_looping_transitions),
      max_time(max_time),
      use_general_costs(use_general_costs),
      pick_split(pick_split),
      search_strategy(search_strategy),
      flaw_strategy(flaw_strategy),
      memory_padding_mb(memory_padding_mb),
      rng(rng),
      debug(debug),
      dot_graph_verbosity(dot_graph_verbosity),
      num_states(0),
      num_non_looping_transitions(0) {
}

vector<CartesianHeuristicFunction> CostSaturation::generate_heuristic_functions(
    const shared_ptr<AbstractTask> &task) {
    // For simplicity this is a member object. Make sure it is in a valid state.
    assert(heuristic_functions.empty());

    utils::CountdownTimer timer(max_time);

    TaskProxy task_proxy(*task);

    task_properties::verify_no_axioms(task_proxy);
    task_properties::verify_no_conditional_effects(task_proxy);

    reset(task_proxy);

    State initial_state = TaskProxy(*task).get_initial_state();

    function<bool()> should_abort =
        [&] () {
            return num_states >= max_states ||
                   num_non_looping_transitions >= max_non_looping_transitions ||
                   timer.is_expired() ||
                   !utils::extra_memory_padding_is_reserved() ||
                   state_is_dead_end(initial_state);
        };

    utils::reserve_extra_memory_padding(memory_padding_mb);
    for (const shared_ptr<SubtaskGenerator> &subtask_generator : subtask_generators) {
        SharedTasks subtasks = subtask_generator->get_subtasks(task);
        build_abstractions(subtasks, timer, should_abort);
        if (should_abort())
            break;
    }
    if (utils::extra_memory_padding_is_reserved())
        utils::release_extra_memory_padding();
    print_statistics(timer.get_elapsed_time());

    vector<CartesianHeuristicFunction> functions;
    swap(heuristic_functions, functions);

    return functions;
}

void CostSaturation::reset(const TaskProxy &task_proxy) {
    remaining_costs = task_properties::get_operator_costs(task_proxy);
    num_states = 0;
}

void CostSaturation::reduce_remaining_costs(
    const vector<int> &saturated_costs) {
    assert(remaining_costs.size() == saturated_costs.size());
    for (size_t i = 0; i < remaining_costs.size(); ++i) {
        int &remaining = remaining_costs[i];
        const int &saturated = saturated_costs[i];
        assert(saturated <= remaining);
        /* Since we ignore transitions from states s with h(s)=INF, all
           saturated costs (h(s)-h(s')) are finite or -INF. */
        assert(saturated != INF);
        if (remaining == INF) {
            // INF - x = INF for finite values x.
        } else if (saturated == -INF) {
            remaining = INF;
        } else {
            remaining -= saturated;
        }
        assert(remaining >= 0);
    }
}

shared_ptr<AbstractTask> CostSaturation::get_remaining_costs_task(
    shared_ptr<AbstractTask> &parent) const {
    vector<int> costs = remaining_costs;
    return make_shared<extra_tasks::ModifiedOperatorCostsTask>(
        parent, move(costs));
}

bool CostSaturation::state_is_dead_end(const State &state) const {
    for (const CartesianHeuristicFunction &function : heuristic_functions) {
        if (function.get_value(state) == INF)
            return true;
    }
    return false;
}

void CostSaturation::build_abstractions(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const utils::CountdownTimer &timer,
    function<bool()> should_abort) {
    int rem_subtasks = subtasks.size();
    for (shared_ptr<AbstractTask> subtask : subtasks) {
        subtask = get_remaining_costs_task(subtask);
        assert(num_states < max_states);

        CEGAR cegar(
            subtask,
            max(1, (max_states - num_states) / rem_subtasks),
            max(1, (max_non_looping_transitions - num_non_looping_transitions) /
                rem_subtasks),
            timer.get_remaining_time() / rem_subtasks,
            pick_split,
            search_strategy,
            flaw_strategy,
            rng,
            debug,
            dot_graph_verbosity);

        unique_ptr<Abstraction> abstraction = cegar.extract_abstraction();
        num_states += abstraction->get_num_states();
        num_non_looping_transitions += abstraction->get_transition_system().get_num_non_loops();
        assert(num_states <= max_states);

        vector<int> costs = task_properties::get_operator_costs(TaskProxy(*subtask));
        vector<int> init_distances = compute_distances(
            abstraction->get_transition_system().get_outgoing_transitions(),
            costs,
            {abstraction->get_initial_state().get_id()});
        vector<int> goal_distances = compute_distances(
            abstraction->get_transition_system().get_incoming_transitions(),
            costs,
            abstraction->get_goals());
        vector<int> saturated_costs = compute_saturated_costs(
            abstraction->get_transition_system(),
            init_distances,
            goal_distances,
            use_general_costs);

        reduce_remaining_costs(saturated_costs);

        int num_unsolvable_states = count(goal_distances.begin(), goal_distances.end(), INF);
        utils::g_log << "Unsolvable Cartesian states: " << num_unsolvable_states << endl;
        utils::g_log << "Initial h value: "
                     << goal_distances[abstraction->get_initial_state().get_id()]
                     << endl << endl;

        heuristic_functions.emplace_back(
            abstraction->extract_refinement_hierarchy(),
            move(goal_distances));
        --rem_subtasks;

        if (should_abort()) {
            break;
        }
    }
}

void CostSaturation::print_statistics(utils::Duration init_time) const {
    utils::g_log << "Done initializing additive Cartesian heuristic" << endl;
    utils::g_log << "Time for initializing additive Cartesian heuristic: "
                 << init_time << endl;
    utils::g_log << "Cartesian abstractions built: " << heuristic_functions.size() << endl;
    utils::g_log << "Cartesian states: " << num_states << endl;
    utils::g_log << "Total number of non-looping transitions: "
                 << num_non_looping_transitions << endl;
    utils::g_log << endl;
}
}
