#include "cegar.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "cartesian_set.h"
#include "shortest_paths.h"
#include "transition_system.h"
#include "utils.h"

#include "../task_utils/task_properties.h"
#include "../utils/language.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace std;

namespace cartesian_abstractions {
CEGAR::CEGAR(
    const shared_ptr<AbstractTask> &task,
    int max_states,
    int max_non_looping_transitions,
    double max_time,
    PickFlawedAbstractState pick_flawed_abstract_state,
    PickSplit pick_split,
    PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state,
    int max_state_expansions,
    bool store_spt_children,
    bool store_spt_parents,
    utils::RandomNumberGenerator &rng,
    utils::LogProxy &log,
    DotGraphVerbosity dot_graph_verbosity)
    : task_proxy(*task),
      domain_sizes(get_domain_sizes(task_proxy)),
      max_states(max_states),
      max_non_looping_transitions(max_non_looping_transitions),
      pick_flawed_abstract_state(pick_flawed_abstract_state),
      abstraction(utils::make_unique_ptr<Abstraction>(task, log)),
      timer(max_time),
      log(log),
      dot_graph_verbosity(dot_graph_verbosity) {
    assert(max_states >= 1);
    shortest_paths = utils::make_unique_ptr<ShortestPaths>(
        task_properties::get_operator_costs(task_proxy),
        store_spt_children, store_spt_parents, timer, log);
    flaw_search = utils::make_unique_ptr<FlawSearch>(
        task, *abstraction, *shortest_paths, rng,
        pick_flawed_abstract_state, pick_split, tiebreak_split,
        max_concrete_states_per_abstract_state, max_state_expansions, log);

    if (log.is_at_least_normal()) {
        log << "Start building abstraction." << endl;
        log << "Maximum number of states: " << max_states << endl;
        log << "Maximum number of transitions: "
            << max_non_looping_transitions << endl;
        log << "Maximum time: " << timer.get_remaining_time() << endl;
    }

    refinement_loop();
    if (log.is_at_least_normal()) {
        log << "Done building abstraction." << endl;
        log << "Time for building abstraction: " << timer.get_elapsed_time() << endl;
        print_statistics();
    }
}

CEGAR::~CEGAR() {
}

unique_ptr<Abstraction> CEGAR::extract_abstraction() {
    assert(abstraction);
    return move(abstraction);
}

vector<int> CEGAR::get_goal_distances() const {
    assert(shortest_paths);
    return shortest_paths->get_goal_distances();
}

void CEGAR::separate_facts_unreachable_before_goal() const {
    assert(abstraction->get_goals().size() == 1);
    assert(abstraction->get_num_states() == 1);
    assert(task_proxy.get_goals().size() == 1);
    FactProxy goal = task_proxy.get_goals()[0];
    utils::HashSet<FactProxy> reachable_facts = get_relaxed_possible_before(
        task_proxy, goal);
    for (VariableProxy var : task_proxy.get_variables()) {
        if (!may_keep_refining())
            break;
        int var_id = var.get_id();
        vector<int> unreachable_values;
        for (int value = 0; value < var.get_domain_size(); ++value) {
            FactProxy fact = var.get_fact(value);
            if (reachable_facts.count(fact) == 0)
                unreachable_values.push_back(value);
        }
        if (!unreachable_values.empty())
            abstraction->refine(abstraction->get_initial_state(), var_id, unreachable_values);
    }
    abstraction->mark_all_states_as_goals();
    /*
      Split off the goal fact from the initial state. Then the new initial
      state is the only non-goal state and no goal state will have to be split
      later.

      For all states s in which the landmark might have been achieved we need
      h(s)=0. If the limits don't allow splitting off all facts unreachable
      before the goal to achieve this, we instead preserve h(s)=0 for *all*
      states s and cannot split off the goal fact from the abstract initial
      state.
    */
    assert(abstraction->get_initial_state().includes(task_proxy.get_initial_state()));
    assert(reachable_facts.count(goal));
    if (may_keep_refining()) {
        abstraction->refine(abstraction->get_initial_state(), goal.get_variable().get_id(), {goal.get_value()});
    }
}

bool CEGAR::may_keep_refining() const {
    if (abstraction->get_num_states() >= max_states) {
        if (log.is_at_least_normal()) {
            log << "Reached maximum number of states." << endl;
        }
        return false;
    } else if (abstraction->get_num_transitions() >= max_non_looping_transitions) {
        if (log.is_at_least_normal()) {
            log << "Reached maximum number of transitions." << endl;
        }
        return false;
    } else if (timer.is_expired()) {
        if (log.is_at_least_normal()) {
            log << "Reached time limit." << endl;
        }
        return false;
    } else if (!utils::extra_memory_padding_is_reserved()) {
        if (log.is_at_least_normal()) {
            log << "Reached memory limit." << endl;
        }
        return false;
    }
    return true;
}

void CEGAR::refinement_loop() {
    /*
      For landmark tasks we have to map all states in which the
      landmark might have been achieved to arbitrary abstract goal
      states. For the other types of subtasks our method won't find
      unreachable facts, but calling it unconditionally for subtasks
      with one goal doesn't hurt and simplifies the implementation.

      In any case, we separate all goal states from non-goal states
      to simplify the implementation. This way, we don't have to split
      goal states later.
    */
    if (task_proxy.get_goals().size() == 1) {
        separate_facts_unreachable_before_goal();
    } else {
        // Iteratively split off the next goal fact from the current goal state.
        assert(abstraction->get_num_states() == 1);
        const AbstractState *current = &abstraction->get_initial_state();
        for (FactProxy goal : task_proxy.get_goals()) {
            if (!may_keep_refining()) {
                break;
            }
            FactPair fact = goal.get_pair();
            auto pair = abstraction->refine(*current, fact.var, {fact.value});
            dump_dot_graph();
            current = &abstraction->get_state(pair.second);
        }
        assert(!abstraction->get_goals().count(abstraction->get_initial_state().get_id()));
        assert(abstraction->get_goals().size() == 1);
    }

    // Initialize abstract goal distances and shortest path tree.
    if (log.is_at_least_debug()) {
        log << "Initialize abstract goal distances and shortest path tree." << endl;
    }
    shortest_paths->recompute(
        *abstraction,
        abstraction->get_goals());
    assert(shortest_paths->test_distances(*abstraction, abstraction->get_goals()));

    utils::Timer find_trace_timer(false);
    utils::Timer find_flaw_timer(false);
    utils::Timer refine_timer(false);
    utils::Timer update_goal_distances_timer(false);

    while (may_keep_refining()) {
        find_trace_timer.resume();
        unique_ptr<Solution> solution;
        solution = shortest_paths->extract_solution(
            abstraction->get_initial_state().get_id(), abstraction->get_goals());
        find_trace_timer.stop();

        if (solution) {
            int new_abstract_solution_cost =
                shortest_paths->get_32bit_goal_distance(abstraction->get_initial_state().get_id());
            if (new_abstract_solution_cost > old_abstract_solution_cost) {
                old_abstract_solution_cost = new_abstract_solution_cost;
                if (log.is_at_least_verbose()) {
                    log << "Lower bound: " << old_abstract_solution_cost << endl;
                }
            }
        } else {
            log << "Abstract task is unsolvable." << endl;
            break;
        }

        find_flaw_timer.resume();

        unique_ptr<Split> split;
        if (pick_flawed_abstract_state ==
            PickFlawedAbstractState::FIRST_ON_SHORTEST_PATH) {
            split = flaw_search->get_split_legacy(*solution);
        } else {
            split = flaw_search->get_split(timer);
        }

        find_flaw_timer.stop();

        // Now split==nullptr iff we found a solution or ran out of time or memory.
        // The assertion might fail if we run out of resources between the last check and the assertion.
        assert((split && utils::extra_memory_padding_is_reserved() && !timer.is_expired()) ||
               (!split && (!utils::extra_memory_padding_is_reserved() || timer.is_expired())) ||
               (!split && utils::extra_memory_padding_is_reserved() && !timer.is_expired()));

        bool did_switch = check_and_switch_transition_representation();
        if (!split && did_switch) {
            // Start next iteration to find flaw with SG representation.
            continue;
        }

        if (!utils::extra_memory_padding_is_reserved()) {
            log << "Reached memory limit in flaw search." << endl;
            break;
        }

        if (timer.is_expired()) {
            log << "Reached time limit in flaw search." << endl;
            break;
        }

        if (!split) {
            log << "Found concrete solution." << endl;
            break;
        }

        refine_timer.resume();
        int state_id = split->abstract_state_id;
        const AbstractState &abstract_state = abstraction->get_state(state_id);
        assert(!abstraction->get_goals().count(state_id));

        pair<int, int> new_state_ids = abstraction->refine(
            abstract_state, split->var_id, split->values);
        refine_timer.stop();

        dump_dot_graph();

        update_goal_distances_timer.resume();
        shortest_paths->update_incrementally(
            *abstraction, state_id, new_state_ids.first, new_state_ids.second, split->var_id);
        assert(shortest_paths->test_distances(*abstraction, abstraction->get_goals()));
        update_goal_distances_timer.stop();

        if (log.is_at_least_verbose() &&
            abstraction->get_num_states() % 1000 == 0) {
            log << abstraction->get_num_states() << "/" << max_states << " states, "
                << abstraction->get_num_transitions() << "/"
                << max_non_looping_transitions << " transitions" << endl;
        }

        check_and_switch_transition_representation();
    }
    if (log.is_at_least_normal()) {
        log << "Time for finding abstract traces: " << find_trace_timer << endl;
        log << "Time for finding flaws and computing splits: " << find_flaw_timer << endl;
        log << "Time for splitting states: " << refine_timer << endl;
        log << "Time for updating goal distances: " << update_goal_distances_timer << endl;
        log << "Number of refinements: " << abstraction->get_num_states() - 1 << endl;
    }
}

bool CEGAR::check_and_switch_transition_representation() const {
    if (g_hacked_tsr == TransitionRepresentation::TS_THEN_SG &&
        !utils::extra_memory_padding_is_reserved()) {
        log << "Memory limit reached -> compute transitions on demand" << endl;
        abstraction->switch_from_transition_system_to_successor_generator();
        utils::reserve_extra_memory_padding(g_hacked_extra_memory_padding_mb);
        return true;
    } else if (g_hacked_tsr == TransitionRepresentation::TS_THEN_SG &&
               abstraction->get_num_transitions() >= max_non_looping_transitions) {
        log << "Transition limit reached -> compute transitions on demand" << endl;
        abstraction->switch_from_transition_system_to_successor_generator();
        return true;
    }
    return false;
}

void CEGAR::dump_dot_graph() const {
    // Dump/write dot file for current abstraction.
    if (dot_graph_verbosity == DotGraphVerbosity::WRITE_TO_CONSOLE) {
        cout << create_dot_graph(task_proxy, *abstraction) << endl;
    } else if (dot_graph_verbosity == DotGraphVerbosity::WRITE_TO_FILE) {
        write_to_file(
            "graph" + to_string(abstraction->get_num_states()) + ".dot",
            create_dot_graph(task_proxy, *abstraction));
    } else if (dot_graph_verbosity != DotGraphVerbosity::SILENT) {
        ABORT("Invalid dot graph verbosity");
    }
}

void CEGAR::print_statistics() const {
    abstraction->print_statistics();
    flaw_search->print_statistics();
    shortest_paths->print_statistics();
}
}
