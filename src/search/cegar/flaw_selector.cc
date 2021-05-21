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
                                 const Solution &solution) const {
    depth = 0;
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (const Transition &step : solution) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        depth++;
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
                                   const Solution &solution) const {
    depth = 0;
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (const Transition &step : solution) {
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
            // TODO(speckd): select random
            const Transition &tr = wildcard_transitions.at(0);
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
            // TODO(speckd): select random
            const Transition &tr = applicable_wildcard_transitions.at(0);
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            const AbstractState *next_abstract_state =
                &abstraction.get_state(tr.target_id);
            return utils::make_unique_ptr<Flaw>(move(concrete_state),
                                                *abstract_state,
                                                next_abstract_state->regress(op));
        }

        // We have a valid wildcard transition
        // TODO(speckd): select random
        const Transition &tr = valid_wildcard_transitions.at(0);
        OperatorProxy op = task_proxy.get_operators()[tr.op_id];
        const AbstractState *next_abstract_state =
            &abstraction.get_state(tr.target_id);
        State next_concrete_state = concrete_state.get_unregistered_successor(op);
        abstract_state = next_abstract_state;
        concrete_state = move(next_concrete_state);
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
FlawSelector::find_flaw_optimistic_lazy(const Abstraction &abstraction,
                                        const vector<int> &domain_sizes,
                                        const Solution &solution) const {
    depth = 0;
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (const Transition &step : solution) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        depth++;
        const AbstractState *next_abstract_state =
            &abstraction.get_state(step.target_id);

        bool applicable = false;
        bool deviation = true;
        OperatorProxy flaw_op = task_proxy.get_operators()[step.op_id];
        const AbstractState *flaw_abstract_state = abstract_state;

        for (const Transition &wildcard_tr :
             abstraction.get_transition_system().get_outgoing_transitions().at(
                 abstract_state->get_id())) {
            if (!are_wildcard_tr(step, wildcard_tr)) {
                continue;
            }

            OperatorProxy op = task_proxy.get_operators()[wildcard_tr.op_id];

            if (task_properties::is_applicable(op, concrete_state)) {
                applicable = true;
                State next_concrete_state =
                    concrete_state.get_unregistered_successor(op);
                if (next_abstract_state->includes(next_concrete_state)) {
                    deviation = false;
                    abstract_state = next_abstract_state;
                    concrete_state = move(next_concrete_state);
                    break;
                } else {
                    applicable = true;
                    flaw_op = op;
                    flaw_abstract_state = next_abstract_state;
                }
            }
        }

        if (!applicable) {
            return utils::make_unique_ptr<Flaw>(
                move(concrete_state), *flaw_abstract_state,
                get_cartesian_set(domain_sizes, flaw_op.get_preconditions()));
        }
        if (deviation) {
            return utils::make_unique_ptr<Flaw>(
                move(concrete_state), *abstract_state,
                flaw_abstract_state->regress(flaw_op));
        }
    }

    // TODO(speckd): select a wildcard tr that may lead to concreat goal state
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
                                         const Solution &solution) const {
    unique_ptr<Flaw> flaw;
    switch (flaw_strategy) {
    case FlawStrategy::ORIGINAL:
        flaw = find_flaw_original(abstraction, domain_sizes, solution);
        break;
    case FlawStrategy::OPTIMISTIC:
        flaw = find_flaw_optimistic(abstraction, domain_sizes, solution);
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
