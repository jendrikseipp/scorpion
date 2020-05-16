#ifndef CEGAR_MATCH_TREE_H
#define CEGAR_MATCH_TREE_H

#include "types.h"

#include <vector>

struct FactPair;
class OperatorsProxy;

namespace cegar {
class RefinementHierarchy;

/*
  Rewire transitions after each split.
*/
class MatchTree {
    const std::vector<std::vector<FactPair>> preconditions_by_operator;
    const std::vector<std::vector<FactPair>> effects_by_operator;
    const std::vector<std::vector<FactPair>> postconditions_by_operator;
    const RefinementHierarchy &refinement_hierarchy;

    // Transitions from and to other abstract states.
    std::vector<Operators> incoming;
    std::vector<Operators> outgoing;

    const bool debug;

    void enlarge_vectors_by_one();

    void add_operators_in_trivial_abstraction();

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;

    int get_state_id(NodeID node_id) const;

    Operators get_incoming_operators(const AbstractState &state) const;
    Operators get_outgoing_operators(const AbstractState &state) const;

public:
    MatchTree(
        const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
        bool debug);

    // Update match tree after v has been split for var.
    void split(const CartesianSets &cartesian_sets, const AbstractState &v, int var);

    Transitions get_incoming_transitions(
        const CartesianSets &cartesian_sets, const AbstractState &state) const;
    Transitions get_outgoing_transitions(
        const CartesianSets &cartesian_sets, const AbstractState &state) const;

    int get_num_nodes() const;
    int get_num_operators() const;

    void print_statistics() const;
    void dump() const;
};
}

#endif
