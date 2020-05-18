#ifndef CEGAR_MATCH_TREE_H
#define CEGAR_MATCH_TREE_H

#include "cartesian_set.h"
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
    const std::vector<std::vector<FactPair>> preconditions;
    const std::vector<std::vector<FactPair>> effects;
    const std::vector<std::vector<FactPair>> postconditions;
    const RefinementHierarchy &refinement_hierarchy;
    const CartesianSets &cartesian_sets;

    // Transitions from and to other abstract states.
    std::vector<Operators> incoming;
    std::vector<Operators> outgoing;

    mutable CartesianSet tmp_cartesian_set;

    const bool debug;

    void resize_vectors(int new_size);

    void add_operators_in_trivial_abstraction();

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;

    int get_state_id(NodeID node_id) const;

    Operators get_incoming_operators(const AbstractState &state) const;
    Operators get_outgoing_operators(const AbstractState &state) const;

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

    int get_num_nodes() const;
    int get_num_operators() const;

    void print_statistics() const;
    void dump() const;
};
}

#endif
