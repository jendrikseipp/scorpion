#ifndef CEGAR_MATCH_TREE_H
#define CEGAR_MATCH_TREE_H

#include "abstract_state.h"
#include "cartesian_set.h"
#include "refinement_hierarchy.h"
#include "transition.h"
#include "types.h"

#include <vector>

struct FactPair;
class OperatorsProxy;

namespace successor_generator {
class SuccessorGenerator;
}

namespace cegar {
class RefinementHierarchy;

/*
  Rewire transitions after each split.
*/
class MatchTree {
    const int num_variables;
    // TODO: group this info in new Operator class?
    const std::vector<Facts> preconditions;
    const std::vector<Facts> effects;
    const std::vector<Facts> postconditions;
    const std::vector<std::vector<int>> effect_vars_without_preconditions;
    const std::vector<int> operator_costs;
    const RefinementHierarchy &refinement_hierarchy;
    const CartesianSets &cartesian_sets;
    std::shared_ptr<AbstractTask> inverted_task;
    const successor_generator::SuccessorGenerator &forward_successor_generator;
    const successor_generator::SuccessorGenerator &backward_successor_generator;
    const bool sort_applicable_operators_by_increasing_cost;

    const bool debug;

    void resize_vectors(int new_size);

    void add_operators_in_trivial_abstraction();

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;

    int get_state_id(NodeID node_id) const;

    bool incoming_operator_only_loops(const AbstractState &state, int op_id) const;
    Operators get_incoming_operators(const AbstractState &state) const;
    Operators get_outgoing_operators(const AbstractState &state) const;
    Matcher get_incoming_matcher(int op_id) const;
    Matcher get_outgoing_matcher(int op_id) const;
    bool has_transition(
        const AbstractState &src, int op_id, const AbstractState &dest,
        const std::vector<bool> &domains_intersect) const;
    void order_operators(std::vector<int> &operators) const;

public:
    MatchTree(
        const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
        const CartesianSets &cartesian_sets, bool debug);

    // Update match tree after v has been split for var.
    void split(const CartesianSets &cartesian_sets, const AbstractState &v, int var);

    Transitions get_incoming_transitions(
        const CartesianSets &cartesian_sets, const AbstractState &state) const;
    Transitions get_outgoing_transitions(
        const CartesianSets &cartesian_sets, const AbstractState &state) const;
    int get_operator_between_states(const AbstractState &src, const AbstractState &dest, int cost) const;
    std::vector<bool> get_looping_operators(const AbstractStates &states) const;

    template<typename Callback>
    void for_each_outgoing_transition(
        const CartesianSets &cartesian_sets, const AbstractState &state,
        const Callback &callback) const {
        std::vector<int> operators = get_outgoing_operators(state);
        std::vector<int> target_states;
        for (int op_id : operators) {
            CartesianSet tmp_cartesian_set = state.get_cartesian_set();
            for (const FactPair &fact : postconditions[op_id]) {
                tmp_cartesian_set.set_single_value(fact.var, fact.value);
            }
            target_states.clear();
            refinement_hierarchy.for_each_leaf(
                cartesian_sets, tmp_cartesian_set, get_outgoing_matcher(op_id),
                [&](NodeID leaf_id) {
                    int dest_state_id = get_state_id(leaf_id);
                    assert(dest_state_id != state.get_id());
                    target_states.push_back(dest_state_id);
                });
            sort(target_states.begin(), target_states.end());
            for (int target_state : target_states) {
                bool abort = callback(Transition(op_id, target_state));
                if (abort) {
                    return;
                }
            }
        }
    }

    int get_num_nodes() const;
    int get_num_operators() const;

    void print_statistics() const;
    void dump() const;
};
}

#endif
