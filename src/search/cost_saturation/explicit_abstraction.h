#ifndef COST_SATURATION_EXPLICIT_ABSTRACTION_H
#define COST_SATURATION_EXPLICIT_ABSTRACTION_H

#include "abstraction.h"
#include "types.h"

#include "../algorithms/priority_queues.h"

#include <memory>
#include <utility>
#include <vector>

namespace cost_saturation {
struct Successor {
    int op;
    int state;

    Successor(int op, int state)
        : op(op),
          state(state) {
    }

    bool operator<(const Successor &other) const {
        return std::make_pair(op, state) < std::make_pair(other.op, other.state);
    }

    bool operator>=(const Successor &other) const {
        return !(*this < other);
    }
};

std::ostream &operator<<(std::ostream &os, const Successor &successor);

class ExplicitAbstraction : public Abstraction {
    int num_non_label_transitions;
    int num_label_transitions;
    int num_labels;
    phmap::flat_hash_map<int, int> label_size_counts;
    phmap::flat_hash_map<int, int> reused_label_size_counts;

    OpsPool ops_pool;
    OpsToLabelId ops_to_label_id;
    LabelIdToOps label_id_to_ops;
    int next_label_id;

    // State-changing transitions.
    std::vector<std::vector<Successor>> backward_graph;
    
    // Operators inducing state-changing transitions.
    std::vector<bool> active_operators;
    
    // Operators inducing self-loops.
    std::vector<bool> looping_operators;
    
    std::vector<int> goal_states;

    mutable priority_queues::AdaptiveQueue<int> queue;

public:
    ExplicitAbstraction(
        std::unique_ptr<AbstractionFunction> abstraction_function,
        std::vector<std::vector<Successor>> &&backward_graph,
        std::vector<bool> &&looping_operators,
        std::vector<int> &&goal_states, 
        int min_ops_per_label);
    
    virtual int create_or_reuse_label(std::vector<int> &&ops_sorted);
    virtual int get_num_non_label_transitions() const override { return num_non_label_transitions; }
    virtual int get_num_label_transitions() const override { return num_label_transitions; }
    virtual int get_num_labels() const override { return num_labels; }
    virtual phmap::flat_hash_map<int, int> get_label_size_counts() const override { return label_size_counts; }
    virtual phmap::flat_hash_map<int, int> get_reused_label_size_counts() const override { return reused_label_size_counts; }
    virtual std::vector<std::vector<Successor>> label_reduction(
        std::vector<std::vector<Successor>> &graph, int min_ops_per_label);
    virtual std::vector<int> compute_goal_distances(
        const std::vector<int> &costs) const override;
    virtual std::vector<int> compute_saturated_costs(
        const std::vector<int> &h_values) const override;
    virtual int get_num_operators() const override;
    virtual bool operator_is_active(int op_id) const override;
    virtual bool operator_induces_self_loop(int op_id) const override;
    virtual void for_each_transition(const TransitionCallback &callback) const override;
    virtual int get_num_states() const override;
    virtual const std::vector<int> &get_goal_states() const override;
    virtual void dump() const override;
};
}

#endif
