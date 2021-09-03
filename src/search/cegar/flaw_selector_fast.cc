#include "flaw_selector_fast.h"

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
FlawSelector::find_flaw_backtrack_optimistic(
    const Abstraction &abstraction,
    const vector<int> &domain_sizes,
    const Solution &solution,
    utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_pessimistic(
    const Abstraction &abstraction,
    const vector<int> &domain_sizes,
    const Solution &solution,
    utils::RandomNumberGenerator &rng) const {
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_optimistic(const Abstraction &abstraction,
                                   const vector<int> &domain_sizes,
                                   const Solution &solution,
                                   utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();

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
            bool goal_check = cur_solution.size() == solution.size();
            auto cur_flaw = get_possible_flaw(abstraction, concrete_state,
                                              abstract_state, wildcard_tr, domain_sizes, cur_solution,
                                              goal_check);

            // no flaw
            if (cur_flaw == nullptr) {
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

        OperatorProxy op =
            task_proxy.get_operators()[choosen_solution.back().op_id];
        abstract_state =
            &abstraction.get_state(choosen_solution.back().target_id);
        concrete_state = concrete_state.get_unregistered_successor(op);
    }
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_pessimistic(const Abstraction &abstraction,
                                    const vector<int> &domain_sizes,
                                    const Solution &solution,
                                    utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = task_proxy.get_initial_state();

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
            bool goal_check = cur_solution.size() == solution.size();
            auto cur_flaw = get_possible_flaw(abstraction, concrete_state,
                                              abstract_state, wildcard_tr, domain_sizes, cur_solution,
                                              goal_check);
            if (cur_flaw != nullptr) {
                if (cur_flaw->flaw_reason == FlawReason::NOT_APPLICABLE) {
                    return cur_flaw;
                }

                if (worst_flaw == nullptr
                    || is_flaw_better(worst_flaw, cur_flaw)) {
                    worst_flaw = utils::make_unique_ptr<Flaw>(*cur_flaw);
                }
            }
        }
        if (worst_flaw != nullptr) {
            return worst_flaw;
        }
        choosen_solution.push_back(*rng.choose(wildcard_transitions));

        OperatorProxy op =
            task_proxy.get_operators()[choosen_solution.back().op_id];
        abstract_state =
            &abstraction.get_state(choosen_solution.back().target_id);
        concrete_state = concrete_state.get_unregistered_successor(op);
    }
    return nullptr;
}
}
