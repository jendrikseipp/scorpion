#ifndef CEGAR_ABSTRACTION_H
#define CEGAR_ABSTRACTION_H

#include "match_tree.h"
#include "types.h"

#include "../task_proxy.h"

#include "../utils/collections.h"

#include <memory>
#include <vector>

namespace cegar {
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

    const bool debug;

    void initialize_trivial_abstraction(const std::vector<int> &domain_sizes);

public:
    Abstraction(const std::shared_ptr<AbstractTask> &task, bool debug);
    ~Abstraction();

    Abstraction(const Abstraction &) = delete;

    int get_num_states() const;
    const AbstractState &get_initial_state() const;
    const Goals &get_goals() const;
    const AbstractState &get_state(int state_id) const;
    const TransitionSystem &get_transition_system() const;
    std::unique_ptr<RefinementHierarchy> extract_refinement_hierarchy();

    int get_num_operators() const;
    int get_num_transitions() const;
    Transitions get_incoming_transitions(int state_id) const;
    Transitions get_outgoing_transitions(int state_id) const;
    int get_operator_between_states(int src, int dest, int cost) const;
    std::vector<bool> get_looping_operators() const;

    template<typename Callback>
    void for_each_outgoing_transition(int state_id, const Callback &callback) const {
        // TODO: use specialized version again (if it's faster).
        if (match_tree && false) {
            return match_tree->for_each_outgoing_transition(
                cartesian_sets, *states[state_id], callback);
        } else {
            for (const Transition &t : get_outgoing_transitions(state_id)) {
                bool abort = callback(t);
                if (abort) {
                    return;
                }
            }
        }
    }

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
