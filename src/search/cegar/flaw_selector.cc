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
#include <queue>
#include <stack>

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
           CartesianSet &&desired_cartesian_set, FlawReason flaw_reason)
    : concrete_state(move(concrete_state)),
      current_abstract_state(current_abstract_state),
      desired_cartesian_set(move(desired_cartesian_set)),
      flaw_reason(flaw_reason) {
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
    : task(task), task_proxy(*task), flaw_strategy(flaw_strategy), debug(debug) {}

// Define here to avoid include in header.
FlawSelector::~FlawSelector() {}

// Performs breadth-first search to find the first possible flaw
unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_optimistic(const Abstraction &abstraction,
                                             const vector<int> &domain_sizes,
                                             const Solution &solution,
                                             utils::RandomNumberGenerator &rng) const {
}

// Performs breadth-first search to find the first possible flaw
unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_pessimistic(const Abstraction &abstraction,
                                              const vector<int> &domain_sizes,
                                              const Solution &solution,
                                              utils::RandomNumberGenerator &rng) const {
}

unique_ptr<Flaw>
FlawSelector::find_flaw_original(const Abstraction &abstraction,
                                 const vector<int> &domain_sizes,
                                 const Solution &solution, bool rnd_choice,
                                 utils::RandomNumberGenerator &rng) const {
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

        // Determine wildcard transitions and pick one
        vector<Transition> wildcard_transitions;
        if (rnd_choice) {
            get_wildcard_trs(abstraction, abstract_state, solution.at(step_id),
                             wildcard_transitions);
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
                                                    next_abstract_state->regress(op),
                                                    FlawReason::PATH_DEVIATION);
            }
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            if (debug)
                utils::g_log << "  Operator not applicable: " << op.get_name() << endl;
            return utils::make_unique_ptr<Flaw>(
                move(concrete_state), *abstract_state,
                get_cartesian_set(domain_sizes, op.get_preconditions()),
                FlawReason::NOT_APPLICABLE);
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
            get_cartesian_set(domain_sizes, task_proxy.get_goals()),
            FlawReason::GOAL_TEST);
    }
}

unique_ptr<Flaw>
FlawSelector::find_flaw_optimistic(const Abstraction &abstraction,
                                   const vector<int> &domain_sizes,
                                   const Solution &solution,
                                   utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    Solution choosen_solution;
    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        const Transition &step = solution.at(step_id);

        // determine wildcard trs
        vector<Transition> wildcard_transitions;
        get_wildcard_trs(abstraction, abstract_state, step, wildcard_transitions);
        rng.shuffle(wildcard_transitions);

        unique_ptr<Flaw> result = nullptr;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            Solution cur_solution = choosen_solution;
            cur_solution.push_back(wildcard_tr);
            auto cur_flaw = find_flaw_original(abstraction, domain_sizes, cur_solution, false, rng);

            // no flaw
            if (cur_flaw == nullptr) {
                choosen_solution.push_back(wildcard_tr);
                result = nullptr;
                break;
            }

            // no real flaw
            if (cur_solution.size() < solution.size() &&
                cur_flaw->flaw_reason == FlawReason::GOAL_TEST) {
                choosen_solution.push_back(wildcard_tr);
                result = nullptr;
                break;
            }

            if (result == nullptr ||
                (static_cast<int>(result->flaw_reason) < static_cast<int>(cur_flaw->flaw_reason))) {
                result = utils::make_unique_ptr<Flaw>(*cur_flaw.release());
            }
        }
        if (result != nullptr) {
            return result;
        }

        abstract_state = &abstraction.get_state(step.target_id);
    }
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_pessimistic(const Abstraction &abstraction,
                                    const vector<int> &domain_sizes,
                                    const Solution &solution,
                                    utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    Solution choosen_solution;
    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        const Transition &step = solution.at(step_id);

        // determine wildcard trs
        vector<Transition> wildcard_transitions;
        get_wildcard_trs(abstraction, abstract_state, step, wildcard_transitions);
        rng.shuffle(wildcard_transitions);

        unique_ptr<Flaw> result = nullptr;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            Solution cur_solution = choosen_solution;
            cur_solution.push_back(wildcard_tr);
            auto cur_flaw = find_flaw_original(abstraction, domain_sizes, cur_solution, false, rng);
            if (cur_flaw != nullptr) {
                if (cur_flaw->flaw_reason == FlawReason::NOT_APPLICABLE) {
                    return cur_flaw;
                }
                if (cur_flaw->flaw_reason == FlawReason::PATH_DEVIATION) {
                    result = utils::make_unique_ptr<Flaw>(*cur_flaw.release());
                } else {
                    if (solution.size() == cur_solution.size() &&
                        cur_flaw->flaw_reason == FlawReason::GOAL_TEST &&
                        result->flaw_reason != FlawReason::GOAL_TEST) {
                        result = utils::make_unique_ptr<Flaw>(*cur_flaw.release());
                    }
                }
            }
            cur_flaw.release();
            if (result != nullptr) {
                return result;
            }
        }
        choosen_solution.push_back(*rng.choose(wildcard_transitions));
        abstract_state = &abstraction.get_state(step.target_id);
    }
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

void FlawSelector::get_wildcard_trs(const Abstraction &abstraction,
                                    const AbstractState *abstract_state,
                                    const Transition &base_tr,
                                    vector<Transition> &wildcard_trs) const {
    assert(wildcard_trs.empty());
    for (const Transition &wildcard_tr :
         abstraction.get_transition_system().get_outgoing_transitions().at(
             abstract_state->get_id())) {
        if (are_wildcard_tr(base_tr, wildcard_tr)) {
            wildcard_trs.push_back(wildcard_tr);
        }
    }
}

unique_ptr<Flaw> FlawSelector::find_flaw(const Abstraction &abstraction,
                                         const vector<int> &domain_sizes,
                                         const Solution &solution, utils::RandomNumberGenerator &rng) const {
    // Solution is empty plan
    if (solution.size() == 0) {
        return find_flaw_original(abstraction, domain_sizes, solution, false, rng);
    }

    unique_ptr<Flaw> flaw;
    switch (flaw_strategy) {
    case FlawStrategy::BACKTRACK_PESSIMISTIC:
        flaw = find_flaw_backtrack_pessimistic(abstraction, domain_sizes, solution, rng);
        break;
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
}
} // namespace cegar
