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
    const std::vector<std::vector<FactPair>> postconditions_by_operator;
    const RefinementHierarchy &refinement_hierarchy;

    // Transitions from and to other abstract states.
    std::vector<Transitions> incoming;
    std::vector<Transitions> outgoing;

    // Store self-loops (operator indices) separately to save space.
    std::vector<Loops> loops;

    int num_non_loops;
    int num_loops;

    const bool debug;

    bool is_consistent() const;

    void enlarge_vectors_by_one();

    // Add self-loops to single abstract state in trivial abstraction.
    void add_loops_in_trivial_abstraction();

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;

    int get_state_id(NodeID node_id) const;

    void add_transition(int src_id, int op_id, int target_id);
    void add_loop(int state_id, int op_id);

    void rewire_incoming_transitions(
        const CartesianSets &cartesian_sets, int v_id, const AbstractState &v1,
        const AbstractState &v2, int var);
    void rewire_outgoing_transitions(
        const CartesianSets &cartesian_sets,
        int v_id, const AbstractState &v1, const AbstractState &v2, int var);
    void rewire_loops(
        NodeID v_id, const AbstractState &v1, const AbstractState &v2, int var);

public:
    MatchTree(
        const OperatorsProxy &ops, const RefinementHierarchy &refinement_hierarchy,
        bool debug);

    // Update transition system after v has been split for var into v1 and v2.
    void rewire(
        const CartesianSets &cartesian_sets, const AbstractState &v,
        const AbstractState &v1, const AbstractState &v2, int var);

    Transitions get_incoming_transitions(const AbstractState &state) const;
    Transitions get_outgoing_transitions(const AbstractState &state) const;
    const std::vector<Loops> &get_loops() const;

    int get_num_nodes() const;
    int get_num_operators() const;
    int get_num_non_loops() const;
    int get_num_loops() const;

    void print_statistics() const;
    void dump() const;
};
}

#endif
