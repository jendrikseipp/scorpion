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
    // TODO: group this info in new Operator class?
    const std::vector<Facts> preconditions;
    const std::vector<Facts> effects;
    const std::vector<Facts> postconditions;
    const std::vector<int> operator_costs;
    const RefinementHierarchy &refinement_hierarchy;
    const CartesianSets &cartesian_sets;
    std::shared_ptr<AbstractTask> inverted_task;
    const successor_generator::SuccessorGenerator &forward_successor_generator;
    const successor_generator::SuccessorGenerator &backward_successor_generator;
    const bool sort_applicable_operators_by_increasing_cost;

    // Transitions from and to other abstract states.
    std::vector<Operators> incoming;
    std::vector<Operators> outgoing;

    const bool debug;

    void resize_vectors(int new_size);

    void add_operators_in_trivial_abstraction();

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;

    int get_state_id(NodeID node_id) const;

    bool incoming_operator_only_loops(const AbstractState &state, int op_id) const;
    Operators get_incoming_operators(const AbstractState &state) const;
    Operators get_outgoing_operators(const AbstractState &state) const;
    bool has_transition(const AbstractState &src, int op_id, const AbstractState &dest) const;

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

    template<typename Callback>
    void for_each_outgoing_transition(
        const CartesianSets &cartesian_sets, const AbstractState &state,
        const Callback &callback) const {
        bool abort = false;
        std::vector<int> operators = get_outgoing_operators(state);
        if (sort_applicable_operators_by_increasing_cost) {
            sort(operators.begin(), operators.end(), [&](int op1, int op2) {
                     return operator_costs[op1] < operator_costs[op2];
                 });
        }
        for (int op_id : operators) {
            CartesianSet tmp_cartesian_set = state.get_cartesian_set();
            for (const FactPair &fact : postconditions[op_id]) {
                tmp_cartesian_set.set_single_value(fact.var, fact.value);
            }
            refinement_hierarchy.for_each_leaf(
                cartesian_sets, tmp_cartesian_set, [&](NodeID leaf_id) {
                    int dest_state_id = get_state_id(leaf_id);
                    assert(dest_state_id != state.get_id());
                    abort = callback(Transition(op_id, dest_state_id));
                });
            if (abort) {
                return;
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
