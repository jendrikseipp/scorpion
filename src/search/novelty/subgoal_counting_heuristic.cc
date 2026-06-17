#include "subgoal_counting_heuristic.h"

#include "../plugins/plugin.h"
#include "../utils/markup.h"

#include <cassert>

using namespace std;

namespace novelty {
SubgoalCountingHeuristic::SubgoalCountingHeuristic(
    tasks::AxiomHandlingType axioms, const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description, utils::Verbosity verbosity)
    : FFHeuristic(axioms, transform, cache_estimates, description, verbosity),
      num_relaxed_plan_computations(0) {
    for (FactProxy goal : task_proxy.get_goals()) {
        goal_facts.insert(goal.get_pair());
    }
}

SubgoalCountingHeuristic::~SubgoalCountingHeuristic() {
    log << "Relaxed plan computations: " << num_relaxed_plan_computations
        << endl;
}

int SubgoalCountingHeuristic::count_unsatisfied_goals(
    const State &state) const {
    int count = 0;
    for (const FactPair &goal : goal_facts) {
        if (state[goal.var].get_value() != goal.value) {
            ++count;
        }
    }
    return count;
}

// Compute the subgoals of the relaxed plan for the given state. Following
// Fickert and Hoffmann (JAIR 2022, Section 3.1), a subgoal is a fact that
// appears as an effect of some action in the relaxed plan and is either a goal
// or a precondition of some action in the relaxed plan ("necessary effects").
vector<FactPair> SubgoalCountingHeuristic::compute_subgoals(
    const State &state) {
    ++num_relaxed_plan_computations;

    // Compute the additive heuristic, which sets up the reached_by pointers.
    int h_add = compute_add_and_ff(state);
    if (h_add == DEAD_END) {
        return {};
    }

    // Mark the operators in the relaxed plan. Collecting them by iterating over
    // the marker vector yields them in sorted order.
    OperatorsProxy operators = task_proxy.get_operators();
    vector<bool> op_in_plan(operators.size(), false);
    vector<bool> prop_marked(propositions.size(), false);
    for (relaxation_heuristic::PropID goal_id : goal_propositions) {
        mark_relaxed_plan(goal_id, prop_marked, op_in_plan);
    }
    vector<int> relaxed_plan_ops;
    for (size_t op_no = 0; op_no < op_in_plan.size(); ++op_no) {
        if (op_in_plan[op_no]) {
            relaxed_plan_ops.push_back(op_no);
        }
    }

    // Collect the preconditions of all relaxed-plan actions.
    utils::HashSet<FactPair> preconditions;
    for (int op_no : relaxed_plan_ops) {
        for (FactProxy pre : operators[op_no].get_preconditions()) {
            preconditions.insert(pre.get_pair());
        }
    }

    // A subgoal is an effect of a relaxed-plan action that is either a goal or
    // one of these preconditions.
    utils::HashSet<FactPair> subgoals;
    for (int op_no : relaxed_plan_ops) {
        for (EffectProxy eff : operators[op_no].get_effects()) {
            FactPair fact = eff.get_fact().get_pair();
            if (goal_facts.contains(fact) || preconditions.contains(fact)) {
                subgoals.insert(fact);
            }
        }
    }

    return {subgoals.begin(), subgoals.end()};
}

void SubgoalCountingHeuristic::mark_relaxed_plan(
    relaxation_heuristic::PropID prop_id, vector<bool> &prop_marked,
    vector<bool> &op_in_plan) {
    using namespace relaxation_heuristic;
    if (prop_marked[prop_id]) {
        return;
    }
    prop_marked[prop_id] = true;

    OpID op_id = get_proposition(prop_id)->reached_by;
    if (op_id == NO_OP) {
        return;
    }
    for (PropID precond : get_preconditions(op_id)) {
        mark_relaxed_plan(precond, prop_marked, op_in_plan);
    }
    int operator_no = get_operator(op_id)->operator_no;
    if (operator_no != -1) {
        // Not an axiom.
        op_in_plan[operator_no] = true;
    }
}

void SubgoalCountingHeuristic::start_new_plan(
    const State &state, int num_unsatisfied_goals) {
    vector<FactPair> subgoals = compute_subgoals(state);

    // The path restarts at this state, so the unachieved subgoals are the
    // subgoals that are not already true here.
    vector<FactPair> unachieved;
    unachieved.reserve(subgoals.size());
    for (const FactPair &subgoal : subgoals) {
        if (state[subgoal.var].get_value() != subgoal.value) {
            unachieved.push_back(subgoal);
        }
    }

    SubgoalStatus &status = unachieved_subgoals[state];
    status.num_unsatisfied_goals = num_unsatisfied_goals;
    status.initialized = true;
    status.unachieved = move(unachieved);

    if (log.is_at_least_debug()) {
        log << "New plan: " << subgoals.size() << " subgoals, "
            << status.unachieved.size() << " unachieved" << endl;
    }
}

int SubgoalCountingHeuristic::compute_heuristic(const State &ancestor_state) {
    const SubgoalStatus &status = unachieved_subgoals[ancestor_state];
    assert(status.initialized);
    return status.unachieved.size();
}

void SubgoalCountingHeuristic::get_path_dependent_evaluators(
    set<Evaluator *> &evals) {
    evals.insert(this);
}

void SubgoalCountingHeuristic::notify_initial_state(
    const State &initial_state) {
    start_new_plan(initial_state, count_unsatisfied_goals(initial_state));
}

void SubgoalCountingHeuristic::notify_state_transition(
    const State &parent_state, OperatorID, const State &state) {
    const SubgoalStatus &parent_status = unachieved_subgoals[parent_state];
    assert(parent_status.initialized);

    int num_unsatisfied_goals = count_unsatisfied_goals(state);
    if (num_unsatisfied_goals < parent_status.num_unsatisfied_goals) {
        // More top-level goals are satisfied than in the parent, so recompute
        // the relaxed plan and start a new achievement-tracking path here
        // (Fickert and Hoffmann 2022).
        start_new_plan(state, num_unsatisfied_goals);
    } else {
        // Same relaxed plan: a subgoal stays unachieved iff it was unachieved
        // in the parent and is still not true here.
        vector<FactPair> unachieved;
        for (const FactPair &subgoal : parent_status.unachieved) {
            if (state[subgoal.var].get_value() != subgoal.value) {
                unachieved.push_back(subgoal);
            }
        }

        SubgoalStatus &status = unachieved_subgoals[state];
        status.num_unsatisfied_goals = num_unsatisfied_goals;
        status.initialized = true;
        status.unachieved = move(unachieved);

        if (log.is_at_least_debug()) {
            log << "Inherited plan: " << status.unachieved.size()
                << " unachieved" << endl;
        }
    }
}

class SubgoalCountingHeuristicFeature
    : public plugins::TypedFeature<
          Evaluator, novelty::SubgoalCountingHeuristic> {
public:
    SubgoalCountingHeuristicFeature() : TypedFeature("subgoal_counting") {
        document_title("Relaxed Subgoal-Counting Heuristic");
        document_synopsis(
            "Path-dependent relaxed subgoal-counting heuristic h^SC of " +
            utils::format_journal_reference(
                {"Maximilian Fickert", "Jörg Hoffmann"},
                "Online Relaxation Refinement for Satisficing Planning: On "
                "Partial Delete Relaxation, Complete Hill-Climbing, and Novelty "
                "Pruning",
                "https://www.jair.org/index.php/jair/article/view/13153",
                "Journal of Artificial Intelligence Research", "73", "67-115",
                "2022") +
            "which is itself a variant of the #r subgoal counter used in "
            "Best-First Width Search (BFWS) introduced in " +
            utils::format_conference_reference(
                {"Nir Lipovetzky", "Hector Geffner"},
                "Best-First Width Search: Exploration and Exploitation in "
                "Classical Planning",
                "https://ojs.aaai.org/index.php/AAAI/article/view/11027/10886",
                "Proceedings of the Thirty-First AAAI Conference on Artificial "
                "Intelligence (AAAI-17)",
                "3590-3596", "AAAI Press", "2017") +
            "Given a relaxed plan pi computed in a state s, a subgoal is a fact "
            "that appears as an effect of some action in pi and is either a "
            "goal or a precondition of some action in pi. The heuristic value "
            "of a state s' is the number of subgoals that are never true in any "
            "state along the path from s to s'. The relaxed plan is recomputed "
            "in states where the number of satisfied top-level goals increases "
            "over the parent (and in the initial state).\n\n"
            "This h^SC differs from the BFWS #r counter in two ways. First, #r "
            "counts in the opposite direction: it counts the *achieved* "
            "subgoals (those made true at some point along the path), whereas "
            "h^SC counts the subgoals *not yet achieved*, so that lower values "
            "indicate states closer to the goal and it can be used as a "
            "heuristic on its own (in BFWS #r appears only inside a novelty "
            "measure, never directly as a heuristic). Second, #r treats every "
            "effect of the relaxed-plan actions as a subgoal, while h^SC keeps "
            "only the *necessary* effects, i.e. effects that are a goal or a "
            "precondition of some relaxed-plan action, which more accurately "
            "captures the intention of the underlying plan. This heuristic "
            "implements the h^SC variant.");

        relaxation_heuristic::add_relaxation_heuristic_options_to_feature(
            *this, "subgoal_counting");

        document_property("admissible", "no");
        document_property("consistent", "no");
        document_property("safe", "yes");
        document_property("preferred operators", "no");
        document_property("path-dependent", "yes");
        document_language_support("action costs", "supported");
        document_language_support("conditional effects", "supported");
        document_language_support("axioms", "supported");
    }

    virtual shared_ptr<novelty::SubgoalCountingHeuristic> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            novelty::SubgoalCountingHeuristic>(
            relaxation_heuristic::
                get_relaxation_heuristic_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<SubgoalCountingHeuristicFeature> _plugin;
}
