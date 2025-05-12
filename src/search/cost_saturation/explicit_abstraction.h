#ifndef COST_SATURATION_EXPLICIT_ABSTRACTION_H
#define COST_SATURATION_EXPLICIT_ABSTRACTION_H

#include "abstraction.h"

#include "../algorithms/priority_queues.h"

#include <memory>
#include <utility>
#include <vector>

#include <parallel_hashmap/phmap.h>

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

struct VectorHash {
    std::size_t operator()(const std::vector<int> &v) const {
        std::size_t seed = v.size();
        for (int i : v) {
            seed ^= std::hash<int>{}(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};
// using LabelCallback = std::function<void (const Label &)>;
struct Label {
    std::vector<int> operators;
    int cost;

    Label() : operators(), cost(0) {}
    
    Label(std::vector<int> &&operators_, int cost_) : operators(std::move(operators_)), cost(cost_) {
        std::sort(operators.begin(), operators.end());
    } 

    bool operator==(const Label &other) const {
        return operators == other.operators;
    }
};

extern phmap::flat_hash_map<std::vector<int>, int, VectorHash> ops_to_label_id;
extern phmap::flat_hash_map<int, Label> label_id_to_label;
extern int next_label_id;

class ExplicitAbstraction : public Abstraction {
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
        std::vector<int> &&goal_states);

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
