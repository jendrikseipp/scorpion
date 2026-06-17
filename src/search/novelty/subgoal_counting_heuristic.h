#ifndef NOVELTY_SUBGOAL_COUNTING_HEURISTIC_H
#define NOVELTY_SUBGOAL_COUNTING_HEURISTIC_H

#include "../per_state_information.h"

#include "../heuristics/ff_heuristic.h"
#include "../utils/hash.h"

#include <vector>

namespace novelty {
struct SubgoalStatus {
    // Subgoals of the current relaxed plan not yet true in any state along the
    // path from the plan-computation state to this state.
    std::vector<FactPair> unachieved;
    // Number of unsatisfied top-level goals; cached to detect when more goals
    // become satisfied than in the parent.
    int num_unsatisfied_goals = 0;
    bool initialized = false;
};

/*
  Relaxed subgoal-counting heuristic h^SC of Fickert and Hoffmann (JAIR 2022),
  "Online Relaxation Refinement for Satisficing Planning", Section 3.1.

  Given a relaxed plan pi computed in a state s, a subgoal is a fact that
  appears as an effect of some action in pi and is either a goal or a
  precondition of some action in pi. For a state s', the heuristic value is the
  number of subgoals that are never true in any state along the path from s to
  s'. As in BFWS, the relaxed plan is recomputed only in states where the number
  of satisfied top-level goals increases over the parent (and in the initial
  state); the path over which achievement is tracked then restarts at that
  state.
*/
class SubgoalCountingHeuristic : public ff_heuristic::FFHeuristic {
    // Path-dependent subgoal-achievement status for each state.
    PerStateInformation<SubgoalStatus> unachieved_subgoals;

    // Goal facts, for membership tests and counting unsatisfied goals.
    utils::HashSet<FactPair> goal_facts;

    int num_relaxed_plan_computations;

    int count_unsatisfied_goals(const State &state) const;
    std::vector<FactPair> compute_subgoals(const State &state);
    void mark_relaxed_plan(
        relaxation_heuristic::PropID prop_id, std::vector<bool> &prop_marked,
        std::vector<bool> &op_in_plan);
    // Compute a fresh relaxed plan for the given state and store its status,
    // starting a new achievement-tracking path at this state.
    void start_new_plan(const State &state, int num_unsatisfied_goals);

protected:
    virtual int compute_heuristic(const State &ancestor_state) override;

public:
    SubgoalCountingHeuristic(
        tasks::AxiomHandlingType axioms,
        const std::shared_ptr<AbstractTask> &transform, bool cache_estimates,
        const std::string &description, utils::Verbosity verbosity);

    virtual ~SubgoalCountingHeuristic();

    virtual void get_path_dependent_evaluators(
        std::set<Evaluator *> &evals) override;
    virtual void notify_initial_state(const State &initial_state) override;
    virtual void notify_state_transition(
        const State &parent_state, OperatorID op_id,
        const State &state) override;
};
}

#endif
