#include "cost_saturation.h"

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
#include <execution>

using namespace std;

namespace cartesian_abstractions {
static vector<int> compute_saturated_costs(
    const Abstraction &abstraction,
    const vector<int> &h_values,
    bool use_general_costs) {
    const int min_cost = use_general_costs ? -INF : 0;
    vector<int> saturated_costs(abstraction.get_num_operators(), min_cost);
    if (use_general_costs) {
        /* To prevent negative cost cycles, all operators inducing
           self-loops must have non-negative costs. */
        vector<bool> looping_ops = abstraction.get_looping_operators();
        for (int op_id = 0; op_id < abstraction.get_num_operators(); ++op_id) {
            if (looping_ops[op_id]) {
                saturated_costs[op_id] = 0;
            }
        }
    }

    int num_states = h_values.size();
    for (int state_id = 0; state_id < num_states; ++state_id) {
        int h = h_values[state_id];

        /*
          No need to maintain goal distances of dead end states (h == INF).

          We could also ignore unreachable states (g == INF), but we'd first need
          to compute the g values.

          Note that the "succ_h == INF" test below is sufficient for
          ignoring dead end states. The "h == INF" test is a speed
          optimization.
        */
        if (h == INF)
            continue;

        for (const Transition &transition:
             abstraction.get_outgoing_transitions(state_id)) {
            int op_id = transition.op_id;
            int succ_id = transition.target_id;
            int succ_h = h_values[succ_id];

            if (succ_h == INF)
                continue;

            int needed = h - succ_h;
            saturated_costs[op_id] = max(saturated_costs[op_id], needed);
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
    PickFlawedAbstractState pick_flawed_abstract_state,
    PickSplit pick_split,
    PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state,
    int max_state_expansions,
    TransitionRepresentation transition_representation,
    int memory_padding_mb,
    utils::RandomNumberGenerator &rng,
    utils::LogProxy &log,
    DotGraphVerbosity dot_graph_verbosity)
    : subtask_generators(subtask_generators),
      max_states(max_states),
      max_non_looping_transitions(max_non_looping_transitions),
      max_time(max_time),
      use_general_costs(use_general_costs),
      pick_flawed_abstract_state(pick_flawed_abstract_state),
      pick_split(pick_split),
      tiebreak_split(tiebreak_split),
      max_concrete_states_per_abstract_state(max_concrete_states_per_abstract_state),
      max_state_expansions(max_state_expansions),
      transition_representation(transition_representation),
      memory_padding_mb(memory_padding_mb),
      rng(rng),
      log(log),
      dot_graph_verbosity(dot_graph_verbosity),
      fast_downward_new_handler(get_new_handler()),
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
                   state_is_dead_end(initial_state);
        };

    for (const shared_ptr<SubtaskGenerator> &subtask_generator : subtask_generators) {
        SharedTasks subtasks = subtask_generator->get_subtasks(task, log);
        log << "Build abstractions for " << subtasks.size() << " subtasks in "
            << timer.get_remaining_time() << endl;
        cout << endl;
        build_abstractions(subtasks, timer, should_abort);
        if (should_abort())
            break;
    }
    if (utils::extra_memory_padding_is_reserved()) {
        utils::g_log << "Done building abstractions --> release extra memory padding." << endl;
        utils::release_extra_memory_padding();
    }
    set_new_handler(fast_downward_new_handler);
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

static int get_subtask_limit(int limit, int used, int remaining_subtasks) {
    assert(used < limit);
    if (limit == INF) {
        return INF;
    }
    return max(1, (limit - used) / remaining_subtasks);
}

void CostSaturation::build_abstractions(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const utils::CountdownTimer &timer,
    const function<bool()> &should_abort) {
    utils::Timer scf_timer(false);
    int rem_subtasks = subtasks.size();
    for (shared_ptr<AbstractTask> subtask : subtasks) {
        subtask = get_remaining_costs_task(subtask);
        assert(num_states < max_states);

        if (!utils::extra_memory_padding_is_reserved()) {
            utils::g_log << "Reserve extra memory padding for the next abstraction" << endl;
            // Unset new-handler so that a failed allocation throws std::bad_alloc.
            set_new_handler(nullptr);
            try {
                utils::reserve_extra_memory_padding(memory_padding_mb);
            } catch (const bad_alloc &) {
                set_new_handler(fast_downward_new_handler);
                utils::g_log << "Failed to reserve extra memory padding for the next "
                    "abstraction. --> Stop building new abstractions." << endl;
                break;
            }
        }

        double time_limit = timer.get_remaining_time() / rem_subtasks;
        CEGAR cegar(
            subtask,
            get_subtask_limit(max_states, num_states, rem_subtasks),
            get_subtask_limit(
                max_non_looping_transitions, num_non_looping_transitions,
                rem_subtasks),
            time_limit,
            pick_flawed_abstract_state,
            pick_split,
            tiebreak_split,
            max_concrete_states_per_abstract_state,
            max_state_expansions,
            transition_representation,
            rng,
            log,
            dot_graph_verbosity);
        // Reset new-handler if we ran out of memory.
        if (!utils::extra_memory_padding_is_reserved()) {
            set_new_handler(fast_downward_new_handler);
        }

        unique_ptr<Abstraction> abstraction = cegar.extract_abstraction();
        num_states += abstraction->get_num_states();
        num_non_looping_transitions += abstraction->get_num_stored_transitions();
        assert(num_states <= max_states);

        vector<int> goal_distances = cegar.get_goal_distances();
        if (subtask_generators.size() == 1 && subtasks.size() == 1) {
            log << "There is only one abstraction --> skip computing saturated costs." << endl;
        } else {
            scf_timer.resume();
            vector<int> saturated_costs = compute_saturated_costs(
                *abstraction, goal_distances, use_general_costs);
            scf_timer.stop();
            reduce_remaining_costs(saturated_costs);
        }

        int num_unsolvable_states = count(execution::unseq, goal_distances.begin(), goal_distances.end(), INF);
        log << "Unsolvable Cartesian states: " << num_unsolvable_states << endl;
        log << "Initial h value: "
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
    utils::g_log << "Time for computing saturated cost functions: " << scf_timer << endl;
}

void CostSaturation::print_statistics(utils::Duration init_time) const {
    if (log.is_at_least_normal()) {
        log << "Done initializing additive Cartesian heuristic" << endl;
        log << "Time for initializing additive Cartesian heuristic: "
            << init_time << endl;
        log << "Cartesian abstractions: " << heuristic_functions.size() << endl;
        log << "Total number of Cartesian states: " << num_states << endl;
        log << "Total number of non-looping transitions: "
            << num_non_looping_transitions << endl;
        log << endl;
    }
}
}
