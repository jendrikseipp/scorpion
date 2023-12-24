#ifndef CARTESIAN_ABSTRACTIONS_ABSTRACTION_H
#define CARTESIAN_ABSTRACTIONS_ABSTRACTION_H

#include "match_tree.h"
#include "types.h"

#include "../task_proxy.h"

#include "../utils/collections.h"

#include <memory>
#include <vector>

namespace utils {
class LogProxy;
}

namespace cartesian_abstractions {
class AbstractState;
class MatchTree;
class RefinementHierarchy;
class TransitionSystem;

/*
  Store the set of AbstractStates, use AbstractSearch to find abstract
  solutions, find flaws, use SplitSelector to select splits in case of
  ambiguities, break spurious solutions and maintain the
  RefinementHierarchy.
*/
class Abstraction {
    std::unique_ptr<TransitionSystem> transition_system;
    const State concrete_initial_state;
    const std::vector<FactPair> goal_facts;

    // All (as of yet unsplit) abstract states.
    AbstractStates states;
    CartesianSets cartesian_sets;
    // State ID of abstract initial state.
    int init_id;
    // Abstract goal states. Only landmark tasks can have multiple goal states.
    Goals goals;

    /* DAG with inner nodes for all split states and leaves for all
       current states. */
    std::unique_ptr<RefinementHierarchy> refinement_hierarchy;

    std::unique_ptr<MatchTree> match_tree;

    utils::LogProxy &log;
    const bool debug;

    void initialize_trivial_abstraction(const std::vector<int> &domain_sizes);

public:
    Abstraction(const std::shared_ptr<AbstractTask> &task, utils::LogProxy &log);
    ~Abstraction();

    Abstraction(const Abstraction &) = delete;

    int get_num_states() const;
    const AbstractState &get_initial_state() const;
    const Goals &get_goals() const;
    const AbstractState &get_state(int state_id) const;
    const AbstractStates &get_states() const;
    int get_abstract_state_id(const State &state) const;
    const TransitionSystem &get_transition_system() const;
    std::unique_ptr<RefinementHierarchy> extract_refinement_hierarchy();

    const std::vector<FactPair> get_preconditions(int op_id) const;
    int get_num_operators() const;
    int get_num_transitions() const;
    Transitions get_incoming_transitions(int state_id) const;
    Transitions get_outgoing_transitions(int state_id) const;
    bool has_transition(int src, int op_id, int dest) const;
    int get_operator_between_states(int src, int dest, int cost) const;
    std::vector<bool> get_looping_operators() const;

    /* Needed for CEGAR::separate_facts_unreachable_before_goal(). */
    void mark_all_states_as_goals();

    // Split state into two child states.
    std::pair<int, int> refine(
        const AbstractState &state, int var, const std::vector<int> &wanted);

    void switch_from_transition_system_to_successor_generator();

    void print_statistics() const;
};
}

#endif
