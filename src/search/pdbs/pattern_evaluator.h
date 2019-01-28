#ifndef PDBS_PATTERN_EVALUATOR_H
#define PDBS_PATTERN_EVALUATOR_H

#include "types.h"

#include "../task_proxy.h"

#include <vector>

namespace priority_queues {
template<typename Value>
class AdaptiveQueue;
}

namespace successor_generator {
class SuccessorGenerator;
}

namespace pdbs {
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

struct OperatorInfo {
    int concrete_operator_id;
    std::vector<FactPair> preconditions;
    std::vector<FactPair> effects;
public:
    explicit OperatorInfo(const OperatorProxy &op);
};

struct TaskInfo {
    std::vector<OperatorInfo> operator_infos;
public:
    explicit TaskInfo(const TaskProxy &task_proxy);
};

class PatternEvaluator {
    TaskProxy task_proxy;

    std::vector<AbstractBackwardOperator> abstract_backward_operators;
    std::unique_ptr<successor_generator::SuccessorGenerator> backward_successor_generator;

    int num_states;
    std::vector<std::size_t> hash_multipliers;
    std::vector<int> pattern_domain_sizes;
    std::vector<int> goal_states;

    std::vector<int> compute_goal_states(
        const std::vector<std::size_t> &hash_multipliers,
        const std::vector<int> &pattern_domain_sizes,
        const std::vector<int> &variable_to_pattern_index) const;

    void multiply_out(
        const Pattern &pattern,
        const std::vector<size_t> &hash_multipliers,
        int pos, int cost, int op_id,
        std::vector<FactPair> &prev_pairs,
        std::vector<FactPair> &pre_pairs,
        std::vector<FactPair> &eff_pairs,
        const std::vector<FactPair> &effects_without_pre,
        const std::vector<int> &domain_sizes,
        std::vector<AbstractBackwardOperator> &abstract_backward_operators,
        std::vector<std::vector<FactPair>> &preconditions_per_operator) const;

    void build_abstract_operators(
        const Pattern &pattern,
        const std::vector<std::size_t> &hash_multipliers,
        const OperatorInfo &op,
        int cost,
        const std::vector<int> &variable_to_pattern_index,
        const std::vector<int> &domain_sizes,
        std::vector<AbstractBackwardOperator> &abstract_backward_operators,
        std::vector<std::vector<FactPair>> &preconditions_per_operator) const;

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
        const TaskInfo &task_info,
        const pdbs::Pattern &pattern);
    ~PatternEvaluator();

    bool is_useful(
        priority_queues::AdaptiveQueue<size_t> &pq,
        const std::vector<int> &costs) const;
};
}

#endif
