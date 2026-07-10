#ifndef INVARIANTS_INVARIANT_FINDER_H
#define INVARIANTS_INVARIANT_FINDER_H

#include "invariants.h"

#include "../pddl/action.h"
#include "../pddl/condition.h"
#include "../pddl/task.h"
#include "../utils/cpython_random.h"

#include <cstddef>
#include <random>
#include <unordered_map>
#include <vector>

namespace translate::invariants {
class BalanceChecker {
public:
    BalanceChecker(
        const pddl::Task &task, const std::vector<std::vector<std::vector<int>>>
                                    *reachable_action_parameters);

    // Actions that add `predicate`, as indices for action() / heavy_action().
    // Indices (rather than pointers) let callers deduplicate threats with the
    // epoch stamp below instead of a per-call hash set.
    const std::vector<int> &get_threats(const std::string &predicate) const;
    const pddl::Action &action(int index) const {
        return patched_actions_[index];
    }
    const pddl::Action &heavy_action(int index) const {
        return heavy_actions_[index];
    }
    int next_index(std::size_t upper_bound);

    /*
      Alloc-free membership test over action indices, shared by every
      check_balance call: begin_action_set() opens a fresh (empty) set;
      insert_action(i) returns true iff i was not yet in it. Schema-heavy
      domains (tens of thousands of actions) run this once per candidate;
      the per-call hash set it replaces was the allocation hot spot.
    */
    void begin_action_set() {
        ++epoch_;
    }
    bool insert_action(int index) {
        if (action_stamp_[index] == epoch_)
            return false;
        action_stamp_[index] = epoch_;
        return true;
    }

private:
    // Constructor phases (each fills the members below).
    void build_patched_actions(
        const pddl::Task &task, const std::vector<std::vector<std::vector<int>>>
                                    *reachable_action_parameters);
    void build_heavy_actions();
    void build_predicate_map();

    std::vector<pddl::Action> patched_actions_; // owns patched actions
    std::vector<pddl::Action> heavy_actions_; // owns heavy versions (aligned)
    std::unordered_map<std::string, std::vector<int>>
        predicates_to_add_actions_;
    std::vector<long> action_stamp_; // epoch of last insert, per action
    long epoch_ = 0;
    std::mt19937 random_; // legacy RNG (--no-cpython-rng)
    utils::CPythonRandom cpython_random_; // default: matches Python
    std::vector<int> empty_;
};

/*
  Find mutex group candidate invariants. Returns the list of confirmed
  invariants.
*/
std::vector<Invariant> find_invariants(
    const pddl::Task &task, const std::vector<std::vector<std::vector<int>>>
                                *reachable_action_parameters);

/*
  Convert confirmed invariants into mutex groups (lists of ground atoms).
*/
std::vector<std::vector<pddl::ConditionPtr>> get_groups(
    const pddl::Task &task, const std::vector<std::vector<std::vector<int>>>
                                *reachable_action_parameters);
}

#endif
