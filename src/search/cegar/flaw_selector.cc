#include "flaw_selector.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "split_selector.h"
#include "transition.h"
#include "transition_system.h"
#include "types.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
// Create the Cartesian set that corresponds to the given preconditions or
// goals.
static CartesianSet get_cartesian_set(const vector<int> &domain_sizes,
                                      const ConditionsProxy &conditions) {
    CartesianSet cartesian_set(domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(condition.get_variable().get_id(),
                                       condition.get_value());
    }
    return cartesian_set;
}

Flaw::Flaw(State &&concrete_state, const AbstractState &current_abstract_state,
           CartesianSet &&desired_cartesian_set)
    : concrete_state(move(concrete_state)),
      current_abstract_state(current_abstract_state),
      desired_cartesian_set(move(desired_cartesian_set)) {
    assert(current_abstract_state.includes(this->concrete_state));
}

vector<Split> Flaw::get_possible_splits() const {
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

FlawSelector::FlawSelector(const shared_ptr<AbstractTask> &task,
                           FlawStrategy flaw_strategy, bool debug)
    : task(task), task_proxy(*task), flaw_strategy(flaw_strategy), debug(debug),
      depth(0) {}

// Define here to avoid include in header.
FlawSelector::~FlawSelector() {}

unique_ptr<Flaw>
FlawSelector::find_flaw_original(const Abstraction &abstraction,
                                 const vector<int> &domain_sizes,
                                 const Solution &solution, bool rnd_choice,
                                 utils::RandomNumberGenerator &rng) const {
    depth = 0;
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        depth++;

        // Determine wildcard transitions and pick one
        vector<Transition> wildcard_transitions;
        if (rnd_choice) {
            for (const Transition &wildcard_tr :
                 abstraction.get_transition_system().get_outgoing_transitions().at(
                     abstract_state->get_id())) {
                if (are_wildcard_tr(solution.at(step_id), wildcard_tr)) {
                    wildcard_transitions.push_back(wildcard_tr);
                }
            }
        }

        const Transition &step = rnd_choice ? *rng.choose(wildcard_transitions) : solution.at(step_id);

        OperatorProxy op = task_proxy.get_operators()[step.op_id];
        const AbstractState *next_abstract_state =
            &abstraction.get_state(step.target_id);
        if (task_properties::is_applicable(op, concrete_state)) {
            if (debug)
                utils::g_log << "  Move to " << *next_abstract_state << " with "
                             << op.get_name() << endl;
            State next_concrete_state = concrete_state.get_unregistered_successor(op);
            if (!next_abstract_state->includes(next_concrete_state)) {
                if (debug)
                    utils::g_log << "  Paths deviate." << endl;
                return utils::make_unique_ptr<Flaw>(move(concrete_state),
                                                    *abstract_state,
                                                    next_abstract_state->regress(op));
            }
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            if (debug)
                utils::g_log << "  Operator not applicable: " << op.get_name() << endl;
            return utils::make_unique_ptr<Flaw>(
                move(concrete_state), *abstract_state,
                get_cartesian_set(domain_sizes, op.get_preconditions()));
        }
    }
    assert(abstraction.get_goals().count(abstract_state->get_id()));
    if (task_properties::is_goal_state(task_proxy, concrete_state)) {
        // We found a concrete solution.
        return nullptr;
    } else {
        if (debug)
            utils::g_log << "  Goal test failed." << endl;
        return utils::make_unique_ptr<Flaw>(
            move(concrete_state), *abstract_state,
            get_cartesian_set(domain_sizes, task_proxy.get_goals()));
    }
}

unique_ptr<Flaw>
FlawSelector::find_flaw_optimistic(const Abstraction &abstraction,
                                   const vector<int> &domain_sizes,
                                   const Solution &solution,
                                   utils::RandomNumberGenerator &rng) const {
    depth = 0;
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        const Transition &step = solution.at(step_id);
        if (!utils::extra_memory_padding_is_reserved())
            break;

        depth++;

        // Determine wildcard transitions
        vector<Transition> wildcard_transitions;
        for (const Transition &wildcard_tr :
             abstraction.get_transition_system().get_outgoing_transitions().at(
                 abstract_state->get_id())) {
            if (are_wildcard_tr(step, wildcard_tr)) {
                wildcard_transitions.push_back(wildcard_tr);
            }
        }

        // Determine applicable wildcard transitions
        vector<Transition> applicable_wildcard_transitions;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];
            if (task_properties::is_applicable(op, concrete_state)) {
                applicable_wildcard_transitions.push_back(wildcard_tr);
            }
        }

        // No wildcard transition is applicable
        if (applicable_wildcard_transitions.empty()) {
            const Transition &tr = *rng.choose(wildcard_transitions);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(step.target_id);
            return utils::make_unique_ptr<Flaw>(move(concrete_state),
                                                *abstract_state,
                                                next_abstract_state->regress(op));
        }

        // Determine wildcard transition without deviation
        vector<Transition> valid_wildcard_transitions;
        for (const Transition &wildcard_tr : applicable_wildcard_transitions) {
            OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(wildcard_tr.target_id);
            State next_concrete_state = concrete_state.get_unregistered_successor(op);

            if (next_abstract_state->includes(next_concrete_state)) {
                valid_wildcard_transitions.push_back(wildcard_tr);
            }
        }

        // No wildcard transition without deviation
        if (valid_wildcard_transitions.empty()) {
            const Transition &tr = *rng.choose(applicable_wildcard_transitions);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(tr.target_id);
            return utils::make_unique_ptr<Flaw>(move(concrete_state),
                                                *abstract_state,
                                                next_abstract_state->regress(op));
        }

        // We have a valid wildcard transition
        if (step_id < solution.size() - 1) {
            const Transition &tr = *rng.choose(valid_wildcard_transitions);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(tr.target_id);
            State next_concrete_state = concrete_state.get_unregistered_successor(op);
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            // Last step check for goal
            vector<Transition> goal_wildcard_transitions;
            for (const Transition &wildcard_tr : valid_wildcard_transitions) {
                OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];
                State next_concrete_state = concrete_state.get_unregistered_successor(op);
                if (task_properties::is_goal_state(task_proxy, next_concrete_state)) {
                    goal_wildcard_transitions.push_back(wildcard_tr);
                }
            }

            // Return any flaw induced by a valid wildcard transition
            // otherwise we found a concrete solution
            if (goal_wildcard_transitions.empty()) {
                const Transition &tr = *rng.choose(valid_wildcard_transitions);
                OperatorProxy op = task_proxy.get_operators()[tr.op_id];
                const AbstractState *next_abstract_state =
                    &abstraction.get_state(tr.target_id);
                State next_concrete_state = concrete_state.get_unregistered_successor(op);
                abstract_state = next_abstract_state;
                concrete_state = move(next_concrete_state);
                return utils::make_unique_ptr<Flaw>(
                    move(concrete_state), *abstract_state,
                    get_cartesian_set(domain_sizes, task_proxy.get_goals()));
            }
        }
    }

    // Concrete solution
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_pessimistic(const Abstraction &abstraction,
                                    const vector<int> &domain_sizes,
                                    const Solution &solution,
                                    utils::RandomNumberGenerator &rng) const {
    depth = 0;
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        const Transition &step = solution.at(step_id);
        if (!utils::extra_memory_padding_is_reserved())
            break;

        depth++;

        // Determine wildcard transitions
        vector<Transition> wildcard_transitions;
        for (const Transition &wildcard_tr :
             abstraction.get_transition_system().get_outgoing_transitions().at(
                 abstract_state->get_id())) {
            if (are_wildcard_tr(step, wildcard_tr)) {
                wildcard_transitions.push_back(wildcard_tr);
            }
        }

        // Determine not applicable wildcard transitions
        vector<Transition> not_applicable_wildcard_transitions;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];
            if (!task_properties::is_applicable(op, concrete_state)) {
                not_applicable_wildcard_transitions.push_back(wildcard_tr);
            }
        }

        // A wildcard transition is not applicable
        if (!not_applicable_wildcard_transitions.empty()) {
            const Transition &tr = *rng.choose(not_applicable_wildcard_transitions);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(step.target_id);
            return utils::make_unique_ptr<Flaw>(move(concrete_state),
                                                *abstract_state,
                                                next_abstract_state->regress(op));
        }

        // Determine wildcard transition without deviation
        vector<Transition> not_valid_wildcard_transitions;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(wildcard_tr.target_id);
            State next_concrete_state = concrete_state.get_unregistered_successor(op);

            if (!next_abstract_state->includes(next_concrete_state)) {
                not_valid_wildcard_transitions.push_back(wildcard_tr);
            }
        }

        // A wildcard transition with deviation
        if (!not_valid_wildcard_transitions.empty()) {
            const Transition &tr = *rng.choose(not_valid_wildcard_transitions);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(tr.target_id);
            return utils::make_unique_ptr<Flaw>(move(concrete_state),
                                                *abstract_state,
                                                next_abstract_state->regress(op));
        }

        // We have only valid wildcard transition
        if (step_id < solution.size() - 1) {
            const Transition &tr = *rng.choose(wildcard_transitions);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(tr.target_id);
            State next_concrete_state = concrete_state.get_unregistered_successor(op);
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            // Last step check for goal
            vector<Transition> not_goal_wildcard_transitions;
            for (const Transition &wildcard_tr : wildcard_transitions) {
                OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];
                State next_concrete_state = concrete_state.get_unregistered_successor(op);
                if (!task_properties::is_goal_state(task_proxy, next_concrete_state)) {
                    not_goal_wildcard_transitions.push_back(wildcard_tr);
                }
            }

            // Return any flaw induced by a valid wildcard transition
            // otherwise we found a concrete solution
            if (!not_goal_wildcard_transitions.empty()) {
                const Transition &tr = *rng.choose(not_goal_wildcard_transitions);
                OperatorProxy op = task_proxy.get_operators()[tr.op_id];
                const AbstractState *next_abstract_state =
                    &abstraction.get_state(tr.target_id);
                State next_concrete_state = concrete_state.get_unregistered_successor(op);
                abstract_state = next_abstract_state;
                concrete_state = move(next_concrete_state);
                return utils::make_unique_ptr<Flaw>(
                    move(concrete_state), *abstract_state,
                    get_cartesian_set(domain_sizes, task_proxy.get_goals()));
            }
        }
    }

    // All wildcard plans are concrete solutions
    return nullptr;
}

/* Skip if
 * (1) not leading to same state or
 * (2) tr have different costs
 */
bool FlawSelector::are_wildcard_tr(const Transition &tr1,
                                   const Transition &tr2) const {
    return tr1.target_id == tr2.target_id &&
           task->get_operator_cost(tr1.op_id, false) ==
           task->get_operator_cost(tr2.op_id, false);
}

unique_ptr<Flaw> FlawSelector::find_flaw(const Abstraction &abstraction,
                                         const vector<int> &domain_sizes,
                                         const Solution &solution, utils::RandomNumberGenerator &rng) const {
    unique_ptr<Flaw> flaw;
    switch (flaw_strategy) {
    case FlawStrategy::ORIGINAL:
        flaw = find_flaw_original(abstraction, domain_sizes, solution, false, rng);
        break;
    case FlawStrategy::OPTIMISTIC:
        flaw = find_flaw_optimistic(abstraction, domain_sizes, solution, rng);
        break;
    case FlawStrategy::PESSIMISTIC:
        flaw = find_flaw_pessimistic(abstraction, domain_sizes, solution, rng);
        break;
    case FlawStrategy::RANDOM:
        flaw = find_flaw_original(abstraction, domain_sizes, solution, true, rng);
        break;
    default:
        utils::g_log << "Invalid flaw strategy: " << static_cast<int>(flaw_strategy)
                     << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    return flaw;
}

void FlawSelector::print_statistics() const {
    utils::g_log << "Abstract plan depth: " << depth << endl;
}
} // namespace cegar
