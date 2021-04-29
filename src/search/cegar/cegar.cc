#include "cegar.h"

#include "abstraction.h"
#include "abstract_search.h"
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

namespace cegar {
// Create the Cartesian set that corresponds to the given preconditions or goals.
static CartesianSet get_cartesian_set(
    const vector<int> &domain_sizes, const ConditionsProxy &conditions) {
    CartesianSet cartesian_set(domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(
            condition.get_variable().get_id(), condition.get_value());
    }
    return cartesian_set;
}

struct Flaw {
    // Last concrete and abstract state reached while tracing solution.
    State concrete_state;
    const AbstractState &current_abstract_state;
    // Hypothetical Cartesian set we would have liked to reach.
    CartesianSet desired_cartesian_set;

    Flaw(
        State &&concrete_state,
        const AbstractState &current_abstract_state,
        CartesianSet &&desired_cartesian_set)
        : concrete_state(move(concrete_state)),
          current_abstract_state(current_abstract_state),
          desired_cartesian_set(move(desired_cartesian_set)) {
        assert(current_abstract_state.includes(this->concrete_state));
    }

    vector<Split> get_possible_splits() const {
        vector<Split> splits;
        /*
          For each fact in the concrete state that is not contained in the
          desired abstract state, loop over all values in the domain of the
          corresponding variable. The values that are in both the current and
          the desired abstract state are the "wanted" ones, i.e., the ones that
          we want to split off.
        */
        for (FactProxy wanted_fact_proxy : concrete_state) {
            FactPair fact = wanted_fact_proxy.get_pair();
            if (!desired_cartesian_set.test(fact.var, fact.value)) {
                VariableProxy var = wanted_fact_proxy.get_variable();
                int var_id = var.get_id();
                vector<int> wanted;
                for (int value = 0; value < var.get_domain_size(); ++value) {
                    if (current_abstract_state.contains(var_id, value) &&
                        desired_cartesian_set.test(var_id, value)) {
                        wanted.push_back(value);
                    }
                }
                assert(!wanted.empty());
                splits.emplace_back(var_id, move(wanted));
            }
        }
        assert(!splits.empty());
        return splits;
    }
};

CEGAR::CEGAR(
    const shared_ptr<AbstractTask> &task,
    int max_states,
    int max_non_looping_transitions,
    double max_time,
    PickSplit pick,
    SearchStrategy search_strategy,
    utils::RandomNumberGenerator &rng,
    bool debug)
    : task_proxy(*task),
      domain_sizes(get_domain_sizes(task_proxy)),
      max_states(max_states),
      max_non_looping_transitions(max_non_looping_transitions),
      split_selector(task, pick),
      search_strategy(search_strategy),
      abstraction(utils::make_unique_ptr<Abstraction>(task, debug)),
      timer(max_time),
      debug(debug) {
    assert(max_states >= 1);
    if (search_strategy == SearchStrategy::ASTAR) {
        abstract_search = utils::make_unique_ptr<AbstractSearch>(
            task_properties::get_operator_costs(task_proxy));
    } else if (search_strategy == SearchStrategy::INCREMENTAL) {
        shortest_paths = utils::make_unique_ptr<ShortestPaths>(
            task_properties::get_operator_costs(task_proxy), debug);
    } else {
        ABORT("Unknown search strategy");
    }

    utils::g_log << "Start building abstraction." << endl;
    utils::g_log << "Maximum number of states: " << max_states << endl;
    utils::g_log << "Maximum number of transitions: "
                 << max_non_looping_transitions << endl;

    refinement_loop(rng);
    utils::g_log << "Done building abstraction." << endl;
    utils::g_log << "Time for building abstraction: " << timer.get_elapsed_time() << endl;

    print_statistics();
}

CEGAR::~CEGAR() {
}

unique_ptr<Abstraction> CEGAR::extract_abstraction() {
    assert(abstraction);
    return move(abstraction);
}

void CEGAR::separate_facts_unreachable_before_goal() {
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
    */
    abstraction->refine(
        abstraction->get_initial_state(), goal.get_variable().get_id(), {goal.get_value()});
}

bool CEGAR::may_keep_refining() const {
    if (abstraction->get_num_states() >= max_states) {
        utils::g_log << "Reached maximum number of states." << endl;
        return false;
    } else if (abstraction->get_transition_system().get_num_non_loops() >= max_non_looping_transitions) {
        utils::g_log << "Reached maximum number of transitions." << endl;
        return false;
    } else if (timer.is_expired()) {
        utils::g_log << "Reached time limit." << endl;
        return false;
    } else if (!utils::extra_memory_padding_is_reserved()) {
        utils::g_log << "Reached memory limit." << endl;
        return false;
    }
    return true;
}

void CEGAR::refinement_loop(utils::RandomNumberGenerator &rng) {
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
            FactPair fact = goal.get_pair();
            auto pair = abstraction->refine(*current, fact.var, {fact.value});
            if (debug) {
                dump_dot_graph(*abstraction);
            }
            current = &abstraction->get_state(pair.second);
        }
        assert(!abstraction->get_goals().count(abstraction->get_initial_state().get_id()));
        assert(abstraction->get_goals().size() == 1);
    }

    // Initialize abstract goal distances and shortest path tree.
    if (search_strategy == SearchStrategy::INCREMENTAL) {
        shortest_paths->recompute(
            abstraction->get_transition_system().get_incoming_transitions(),
            abstraction->get_goals());
        assert(shortest_paths->test_distances(
                   abstraction->get_transition_system().get_incoming_transitions(),
                   abstraction->get_transition_system().get_outgoing_transitions(),
                   abstraction->get_goals()));
    }

    if (debug) {
        dump_dot_graph(*abstraction);
    }

    utils::Timer find_trace_timer(false);
    utils::Timer find_flaw_timer(false);
    utils::Timer refine_timer(false);
    utils::Timer update_goal_distances_timer(false);

    while (may_keep_refining()) {
        find_trace_timer.resume();
        unique_ptr<Solution> solution;
        if (search_strategy == SearchStrategy::ASTAR) {
            solution = abstract_search->find_solution(
                abstraction->get_transition_system().get_outgoing_transitions(),
                abstraction->get_initial_state().get_id(),
                abstraction->get_goals());
        } else {
            solution = shortest_paths->extract_solution(
                abstraction->get_initial_state().get_id(), abstraction->get_goals());
        }
        find_trace_timer.stop();

        if (solution) {
            update_goal_distances_timer.resume();
            if (search_strategy == SearchStrategy::ASTAR) {
                abstract_search->update_goal_distances_of_states_on_trace(
                    *solution, abstraction->get_initial_state().get_id());
            }
            update_goal_distances_timer.stop();

            if (debug) {
                cout << "Found abstract solution:" << endl;
                for (const Transition &t : *solution) {
                    OperatorProxy op = task_proxy.get_operators()[t.op_id];
                    cout << "  " << t << " (" << op.get_name() << ", " << op.get_cost() << ")" << endl;
                }
            }
        } else {
            utils::g_log << "Abstract task is unsolvable." << endl;
            break;
        }

        find_flaw_timer.resume();
        unique_ptr<Flaw> flaw = find_flaw(*solution);
        find_flaw_timer.stop();

        if (!flaw) {
            utils::g_log << "Found concrete solution for subtask." << endl;
            break;
        }

        refine_timer.resume();
        const AbstractState &abstract_state = flaw->current_abstract_state;
        int state_id = abstract_state.get_id();
        assert(!abstraction->get_goals().count(state_id));
        vector<Split> splits = flaw->get_possible_splits();
        const Split &split = split_selector.pick_split(abstract_state, splits, rng);
        auto new_state_ids = abstraction->refine(abstract_state, split.var_id, split.values);
        refine_timer.stop();

        if (debug) {
            dump_dot_graph(*abstraction);
        }

        update_goal_distances_timer.resume();
        if (search_strategy == SearchStrategy::ASTAR) {
            // Since h-values only increase we can assign the h-value to the children.
            abstract_search->copy_h_value_to_children(
                state_id, new_state_ids.first, new_state_ids.second);
        } else {
            shortest_paths->update_incrementally(
                abstraction->get_transition_system().get_incoming_transitions(),
                abstraction->get_transition_system().get_outgoing_transitions(),
                state_id, new_state_ids.first, new_state_ids.second);
            assert(shortest_paths->test_distances(
                       abstraction->get_transition_system().get_incoming_transitions(),
                       abstraction->get_transition_system().get_outgoing_transitions(),
                       abstraction->get_goals()));
        }
        update_goal_distances_timer.stop();

        if (abstraction->get_num_states() % 1000 == 0) {
            utils::g_log << abstraction->get_num_states() << "/" << max_states << " states, "
                         << abstraction->get_transition_system().get_num_non_loops() << "/"
                         << max_non_looping_transitions << " transitions" << endl;
        }
    }
    utils::g_log << "Time for finding abstract traces: " << find_trace_timer << endl;
    utils::g_log << "Time for finding flaws: " << find_flaw_timer << endl;
    utils::g_log << "Time for splitting states: " << refine_timer << endl;
    utils::g_log << "Time for updating goal distances: " << update_goal_distances_timer << endl;
}

unique_ptr<Flaw> CEGAR::find_flaw(const Solution &solution) {
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction->get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (const Transition &step : solution) {
        if (!utils::extra_memory_padding_is_reserved())
            break;
        OperatorProxy op = task_proxy.get_operators()[step.op_id];
        const AbstractState *next_abstract_state = &abstraction->get_state(step.target_id);
        if (task_properties::is_applicable(op, concrete_state)) {
            if (debug)
                utils::g_log << "  Move to " << *next_abstract_state << " with "
                             << op.get_name() << endl;
            State next_concrete_state = concrete_state.get_unregistered_successor(op);
            if (!next_abstract_state->includes(next_concrete_state)) {
                if (debug)
                    utils::g_log << "  Paths deviate." << endl;
                return utils::make_unique_ptr<Flaw>(
                    move(concrete_state),
                    *abstract_state,
                    next_abstract_state->regress(op));
            }
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            if (debug)
                utils::g_log << "  Operator not applicable: " << op.get_name() << endl;
            return utils::make_unique_ptr<Flaw>(
                move(concrete_state),
                *abstract_state,
                get_cartesian_set(domain_sizes, op.get_preconditions()));
        }
    }
    assert(abstraction->get_goals().count(abstract_state->get_id()));
    if (task_properties::is_goal_state(task_proxy, concrete_state)) {
        // We found a concrete solution.
        return nullptr;
    } else {
        if (debug)
            utils::g_log << "  Goal test failed." << endl;
        return utils::make_unique_ptr<Flaw>(
            move(concrete_state),
            *abstract_state,
            get_cartesian_set(domain_sizes, task_proxy.get_goals()));
    }
}

void CEGAR::print_statistics() {
    abstraction->print_statistics();
}
}
