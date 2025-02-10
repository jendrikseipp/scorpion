#ifndef CARTESIAN_ABSTRACTIONS_MATCH_TREE_H
#define CARTESIAN_ABSTRACTIONS_MATCH_TREE_H

#include "abstract_state.h"
#include "refinement_hierarchy.h"
#include "types.h"

#include <vector>

struct FactPair;
class OperatorsProxy;

namespace successor_generator {
class SuccessorGenerator;
}

namespace cartesian_abstractions {
class RefinementHierarchy;

/*
  Rewire transitions after each split.
*/
class MatchTree {
    const int num_variables;
    const std::vector<Facts> &preconditions;
    const std::vector<Facts> effects;
    const std::vector<Facts> &postconditions;
    const std::vector<std::vector<int>> effect_vars_without_preconditions;
    const std::vector<int> operator_costs;
    const RefinementHierarchy &refinement_hierarchy;
    const CartesianSets &cartesian_sets;
    std::shared_ptr<AbstractTask> inverted_task;
    const successor_generator::SuccessorGenerator &forward_successor_generator;
    const successor_generator::SuccessorGenerator &backward_successor_generator;

    const bool debug;

    void resize_vectors(int new_size);

    void add_operators_in_trivial_abstraction();

    int get_state_id(NodeID node_id) const;

    bool is_applicable(const AbstractState &src, int op_id) const;
    bool has_transition(
        const AbstractState &src, int op_id, const AbstractState &dest,
        const std::vector<bool> *domains_intersect) const;
    bool incoming_operator_only_loops(const AbstractState &state, int op_id) const;
    Matcher get_incoming_matcher(int op_id) const;
    Matcher get_outgoing_matcher(int op_id) const;
    void order_operators(std::vector<int> &operators) const;

public:
    MatchTree(
        const OperatorsProxy &ops,
        const std::vector<Facts> &preconditions_by_operator,
        const std::vector<Facts> &postconditions_by_operator,
        const RefinementHierarchy &refinement_hierarchy,
        const CartesianSets &cartesian_sets,
        bool debug);

    // Update match tree after v has been split for var.
    void split(const AbstractState &v, int var);

    const std::vector<FactPair> &get_effects(int op_id) const {
        return effects[op_id];
    }

    Operators get_incoming_operators(const AbstractState &state) const;
    Operators get_outgoing_operators(const AbstractState &state) const;
    Transitions get_incoming_transitions(
        const AbstractState &state,
        const std::vector<int> &incoming_operators) const;
    Transitions get_incoming_transitions(const AbstractState &state) const;
    Transitions get_outgoing_transitions(
        const AbstractState &state,
        const std::vector<int> &outgoing_operators) const;
    Transitions get_outgoing_transitions(const AbstractState &state) const;
    bool has_transition(
        const AbstractState &src, int op_id, const AbstractState &dest) const;
    std::vector<bool> get_looping_operators(const AbstractStates &states) const;

    int get_num_nodes() const;
    int get_num_operators() const;

    void print_statistics() const;
    void dump() const;
};
}

#endif
