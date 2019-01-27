#ifndef PDBS_PATTERN_EVALUATOR_H
#define PDBS_PATTERN_EVALUATOR_H

#include "types.h"

#include "../task_proxy.h"

#include <vector>

namespace priority_queues {
template<typename Value>
class AdaptiveQueue;
}

namespace pdbs {
class MatchTree;

struct AbstractBackwardOperator {
    int concrete_operator_id;
    int hash_effect;

    AbstractBackwardOperator(
        int concrete_operator_id,
        int hash_effect)
        : concrete_operator_id(concrete_operator_id),
          hash_effect(hash_effect) {
    }
};

class PatternEvaluator {
    using Facts = std::vector<FactPair>;
    using OperatorCallback =
        std::function<void (Facts &, Facts &, Facts &, int, const std::vector<size_t> &, int)>;

    TaskProxy task_proxy;
    pdbs::Pattern pattern;

    std::vector<AbstractBackwardOperator> abstract_backward_operators;
    std::unique_ptr<pdbs::MatchTree> match_tree_backward;

    // Number of abstract states in the PatternEvaluator.
    int num_states;

    std::vector<int> goal_states;

    std::vector<int> compute_goal_states(
        const std::vector<std::size_t> &hash_multipliers,
        const std::vector<int> &pattern_domain_sizes,
        const std::vector<int> &variable_to_pattern_index) const;

    void multiply_out(
        const std::vector<size_t> &hash_multipliers,
        int pos, int cost, int op_id,
        std::vector<FactPair> &prev_pairs,
        std::vector<FactPair> &pre_pairs,
        std::vector<FactPair> &eff_pairs,
        const std::vector<FactPair> &effects_without_pre,
        const std::vector<int> &domain_sizes,
        const OperatorCallback &callback) const;

    void build_abstract_operators(
        const std::vector<std::size_t> &hash_multipliers,
        const OperatorProxy &op, int cost,
        const std::vector<int> &variable_to_pattern_index,
        const std::vector<int> &domain_sizes,
        const OperatorCallback &callback) const;

    /*
      Return true iff all abstract facts hold in the given state.
    */
    bool is_consistent(
        const std::vector<size_t> &hash_multipliers,
        const std::vector<int> &pattern_domain_sizes,
        std::size_t state_index,
        const std::vector<FactPair> &abstract_facts) const;

public:
    PatternEvaluator(
        const TaskProxy &task_proxy,
        const pdbs::Pattern &pattern);
    ~PatternEvaluator();

    bool is_useful(
        priority_queues::AdaptiveQueue<size_t> &pq,
        const std::vector<int> &costs) const;

    const pdbs::Pattern &get_pattern() const;
};
}

#endif
