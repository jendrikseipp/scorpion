#ifndef TRANSLATE_INVARIANTS_INVARIANT_FINDER_H
#define TRANSLATE_INVARIANTS_INVARIANT_FINDER_H

#include "../pddl/action.h"
#include "../pddl/condition.h"
#include "../pddl/task.h"
#include "../utils/cpython_random.h"
#include "invariants.h"

#include <random>
#include <unordered_map>
#include <vector>

namespace translate::invariants {
class BalanceChecker {
public:
    BalanceChecker(const pddl::Task &task,
                   const std::vector<std::vector<std::vector<std::string>>>
                       *reachable_action_parameters);

    const std::vector<const pddl::Action *> &get_threats(
        const std::string &predicate) const;
    const pddl::Action *get_heavy_action(const pddl::Action *action) const;
    int next_index(std::size_t upper_bound);

private:
    std::vector<pddl::Action> patched_actions_; // owns patched actions
    std::vector<pddl::Action> heavy_actions_;   // owns heavy versions
    std::unordered_map<std::string, std::vector<const pddl::Action *>>
        predicates_to_add_actions_;
    std::unordered_map<const pddl::Action *, const pddl::Action *>
        action_to_heavy_;
    std::mt19937 random_;                       // legacy RNG (--no-cpython-rng)
    utils::CPythonRandom cpython_random_;       // default: matches Python
    std::vector<const pddl::Action *> empty_;
};

/*
  Find mutex group candidate invariants. Returns the list of confirmed
  invariants.
*/
std::vector<Invariant> find_invariants(
    const pddl::Task &task,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters);

/*
  Convert confirmed invariants into mutex groups (lists of ground atoms).
*/
std::vector<std::vector<pddl::ConditionPtr>> get_groups(
    const pddl::Task &task,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters);
}

#endif
