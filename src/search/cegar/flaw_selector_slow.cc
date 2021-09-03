#include "flaw_selector_slow.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "split_selector.h"
#include "transition.h"
#include "transition_system.h"
#include "types.h"
#include "utils.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

#include <cassert>
#include <queue>
#include <stack>

using namespace std;

namespace cegar {
unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_optimistic_slow(
    const Abstraction &abstraction,
    const vector<int> &domain_sizes,
    const Solution &solution,
    utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    // Dermine all wildcard_transitions
    vector<vector<Transition>> all_wildcard_transitions;
    for (const Transition &step : solution) {
        vector<Transition> wildcard_transitions;
        get_wildcard_trs(abstraction, abstract_state, step, wildcard_transitions);
        rng.shuffle(wildcard_transitions);
        all_wildcard_transitions.push_back(wildcard_transitions);
        abstract_state = &abstraction.get_state(step.target_id);
    }
    assert(all_wildcard_transitions.size() == solution.size());

    stack<Solution> stack;
    unique_ptr<Flaw> best_flaw;
    stack.push(Solution());
    while (!stack.empty()) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        Solution base_solution = stack.top();
        stack.pop();
        for (const Transition &tr :
             all_wildcard_transitions[base_solution.size()]) {
            Solution cur_solution = base_solution;
            cur_solution.push_back(tr);
            auto cur_flaw = find_flaw_original(abstraction, domain_sizes, cur_solution, false, rng);

            if (cur_flaw == nullptr) {
                return cur_flaw;
            }

            if (cur_solution.size() < solution.size()
                && cur_flaw->flaw_reason == FlawReason::GOAL_TEST) {
                stack.push(cur_solution);
            } else if (best_flaw == nullptr || is_flaw_better(cur_flaw, best_flaw)) {
                best_flaw = utils::make_unique_ptr<Flaw>(*cur_flaw);
            }
        }
    }

    return best_flaw;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_pessimistic_slow(
    const Abstraction &abstraction,
    const vector<int> &domain_sizes,
    const Solution &solution,
    utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    // Dermine all wildcard_transitions
    vector<vector<Transition>> all_wildcard_transitions;
    for (const Transition &step : solution) {
        vector<Transition> wildcard_transitions;
        get_wildcard_trs(abstraction, abstract_state, step, wildcard_transitions);
        rng.shuffle(wildcard_transitions);
        all_wildcard_transitions.push_back(wildcard_transitions);
        abstract_state = &abstraction.get_state(step.target_id);
    }
    assert(all_wildcard_transitions.size() == solution.size());

    queue<Solution> queue;
    unique_ptr<Flaw> worst_flaw;
    queue.push(Solution());
    while (!queue.empty()) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        Solution base_solution = queue.front();
        queue.pop();
        for (const Transition &tr : all_wildcard_transitions[base_solution.size()]) {
            Solution cur_solution = base_solution;
            cur_solution.push_back(tr);
            auto cur_flaw = find_flaw_original(abstraction, domain_sizes, cur_solution, false, rng);

            if (cur_flaw == nullptr) {
                continue;
            }

            if (cur_flaw->flaw_reason == FlawReason::NOT_APPLICABLE) {
                return cur_flaw;
            }

            if (cur_solution.size() < solution.size()
                && cur_flaw->flaw_reason == FlawReason::GOAL_TEST) {
                queue.push(cur_solution);
            } else if (is_flaw_better(worst_flaw, cur_flaw)) {
                worst_flaw = utils::make_unique_ptr<Flaw>(*cur_flaw);
            }
        }
    }
    if (worst_flaw != nullptr) {
        return worst_flaw;
    }

    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_optimistic_slow(const Abstraction &abstraction,
                                        const vector<int> &domain_sizes,
                                        const Solution &solution,
                                        utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    Solution choosen_solution;
    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        const Transition &step = solution.at(step_id);

        // determine wildcard trs
        vector<Transition> wildcard_transitions;
        get_wildcard_trs(abstraction, abstract_state, step, wildcard_transitions);
        rng.shuffle(wildcard_transitions);

        unique_ptr<Flaw> best_flaw = nullptr;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            Solution cur_solution = choosen_solution;
            cur_solution.push_back(wildcard_tr);
            auto cur_flaw = find_flaw_original(abstraction, domain_sizes, cur_solution, false, rng);

            // no flaw
            if (cur_flaw == nullptr) {
                choosen_solution.push_back(wildcard_tr);
                best_flaw = nullptr;
                break;
            }

            // no real flaw
            if (cur_solution.size() < solution.size() &&
                cur_flaw->flaw_reason == FlawReason::GOAL_TEST) {
                choosen_solution.push_back(wildcard_tr);
                best_flaw = nullptr;
                break;
            }

            if (best_flaw == nullptr || is_flaw_better(cur_flaw, best_flaw)) {
                best_flaw = utils::make_unique_ptr<Flaw>(*cur_flaw);
            }
        }

        if (best_flaw != nullptr) {
            return best_flaw;
        }

        abstract_state = &abstraction.get_state(step.target_id);
    }
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_pessimistic_slow(const Abstraction &abstraction,
                                         const vector<int> &domain_sizes,
                                         const Solution &solution,
                                         utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    Solution choosen_solution;
    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        if (!utils::extra_memory_padding_is_reserved())
            break;

        const Transition &step = solution.at(step_id);

        // determine wildcard trs
        vector<Transition> wildcard_transitions;
        get_wildcard_trs(abstraction, abstract_state, step, wildcard_transitions);
        rng.shuffle(wildcard_transitions);

        unique_ptr<Flaw> worst_flaw = nullptr;
        for (const Transition &wildcard_tr : wildcard_transitions) {
            Solution cur_solution = choosen_solution;
            cur_solution.push_back(wildcard_tr);
            auto cur_flaw = find_flaw_original(abstraction, domain_sizes, cur_solution, false, rng);
            if (cur_flaw != nullptr) {
                if (cur_flaw->flaw_reason == FlawReason::NOT_APPLICABLE) {
                    return cur_flaw;
                }

                // No real flaw
                if (solution.size() != cur_solution.size() &&
                    cur_flaw->flaw_reason == FlawReason::GOAL_TEST) {
                    continue;
                }

                if (worst_flaw == nullptr || is_flaw_better(worst_flaw, cur_flaw)) {
                    worst_flaw = utils::make_unique_ptr<Flaw>(*cur_flaw);
                }
            }
        }
        if (worst_flaw != nullptr) {
            return worst_flaw;
        }
        choosen_solution.push_back(*rng.choose(wildcard_transitions));
        abstract_state = &abstraction.get_state(step.target_id);
    }
    return nullptr;
}

// flaw1 > flaw2
bool FlawSelector::is_flaw_better(const unique_ptr<Flaw> &flaw1,
                                  const unique_ptr<Flaw> &flaw2) const {
    if (flaw1 == nullptr && flaw2 != nullptr) {
        return true;
    }

    if (flaw2 == nullptr) {
        return false;
    }

    if (flaw1->flawed_solution.size() > flaw2->flawed_solution.size()) {
        return true;
    }

    if (flaw1->flawed_solution.size() < flaw2->flawed_solution.size()) {
        return false;
    }

    return static_cast<int>(flaw1->flaw_reason) > static_cast<int>(flaw2->flaw_reason);
}
}
