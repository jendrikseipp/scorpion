#ifndef COST_SATURATION_PROJECTION_H
#define COST_SATURATION_PROJECTION_H

#include "abstraction.h"

#include "../abstract_task.h"

#include "../algorithms/array_pool.h"
#include "../pdbs/types.h"

#include <functional>
#include <vector>

class OperatorProxy;
class TaskProxy;
class VariablesProxy;

namespace pdbs {
class MatchTree;
}

namespace cost_saturation {
/* Precompute and store information about a task that is useful for projections. */
class TaskInfo {
    int num_variables;
    int num_operators;
    std::vector<FactPair> goals;

    /* Set bit at position op_id * num_variables + var to true iff the operator
       has a precondition or an effect on variable var. */
    std::vector<bool> mentioned_variables;

    /* Set bit at position op_id * num_variables + var to true iff the operator
       has a precondition and (different) effect on variable var. */
    std::vector<bool> pre_eff_variables;

    /* Set bit at position op_id * num_variables + var to true iff the operator
       has an effect on variable var. */
    std::vector<bool> effect_variables;

    int get_index(int op_id, int var) const {
        return op_id * num_variables + var;
    }
public:
    explicit TaskInfo(const TaskProxy &task_proxy);

    const std::vector<FactPair> &get_goals() const;
    int get_num_operators() const;
    bool operator_mentions_variable(int op_id, int var) const;
    bool operator_induces_self_loop(const pdbs::Pattern &pattern, int op_id) const;
    bool operator_is_active(const pdbs::Pattern &pattern, int op_id) const;
};

// We distinguish between concrete operators, ranked operators and labels.
struct RankedOperator {
    int label;
    int precondition_hash;
    int hash_effect;

    RankedOperator(int label, int precondition_hash, int hash_effect)
        : label(label),
          precondition_hash(precondition_hash),
          hash_effect(hash_effect) {
    }
};


class ProjectionFunction : public AbstractionFunction {
    struct VariableAndMultiplier {
        int pattern_var;
        int hash_multiplier;

        VariableAndMultiplier(int pattern_var, int hash_multiplier)
            : pattern_var(pattern_var),
              hash_multiplier(hash_multiplier) {
        }
    };
    std::vector<VariableAndMultiplier> variables_and_multipliers;

public:
    ProjectionFunction(
        const pdbs::Pattern &pattern, const std::vector<int> &hash_multipliers);

    virtual int get_abstract_state_id(const State &concrete_state) const override;
};


class Projection : public Abstraction {
    using Facts = std::vector<FactPair>;
    using OperatorCallback =
        std::function<void (Facts &, Facts &, Facts &, const std::vector<int> &)>;

    std::shared_ptr<TaskInfo> task_info;
    pdbs::Pattern pattern;
    array_pool_template::ArrayPool<int> label_to_operators;

    std::vector<bool> looping_operators;

    std::vector<RankedOperator> ranked_operators;
    std::unique_ptr<pdbs::MatchTree> match_tree_backward;

    // Number of abstract states in the projection.
    int num_states;

    // Multipliers for each variable for perfect hash function.
    std::vector<int> hash_multipliers;

    // Domain size of each variable in the pattern.
    std::vector<int> pattern_domain_sizes;

    std::vector<int> goal_states;

    std::vector<int> compute_goal_states(
        const std::vector<int> &variable_to_pattern_index) const;

    /*
      Given an abstract state (represented as a vector of facts), compute the
      "next" fact. Return true iff there is a next fact.
    */
    bool increment_to_next_state(std::vector<FactPair> &facts) const;

    /*
      Apply a function to all state-changing transitions in the projection
      (including unreachable and unsolvable transitions).
    */
    template<class Callback>
    void for_each_label_transition(const Callback &callback) const {
        // Reuse vector to save allocations.
        std::vector<FactPair> abstract_facts;

        for (const RankedOperator &ranked_operator : ranked_operators) {
            // Choose any operator covered by the label.
            int concrete_op_id = *label_to_operators.get_slice(ranked_operator.label).begin();
            abstract_facts.clear();
            for (size_t i = 0; i < pattern.size(); ++i) {
                int var = pattern[i];
                if (!task_info->operator_mentions_variable(concrete_op_id, var)) {
                    abstract_facts.emplace_back(i, 0);
                }
            }

            bool has_next_match = true;
            while (has_next_match) {
                int state = ranked_operator.precondition_hash;
                for (const FactPair &fact : abstract_facts) {
                    state += hash_multipliers[fact.var] * fact.value;
                }
                callback(Transition(state,
                                    ranked_operator.label,
                                    state + ranked_operator.hash_effect));
                has_next_match = increment_to_next_state(abstract_facts);
            }
        }
    }

    /*
      Recursive method; called by build_abstract_operators. In the case
      of a precondition with value = -1 in the concrete operator, all
      multiplied-out abstract operators are computed, i.e., for all
      possible values of the variable (with precondition = -1), one
      abstract operator with a concrete value (!= -1) is computed.
    */
    void multiply_out(
        int pos,
        std::vector<FactPair> &prev_pairs,
        std::vector<FactPair> &pre_pairs,
        std::vector<FactPair> &eff_pairs,
        const std::vector<FactPair> &effects_without_pre,
        const VariablesProxy &variables,
        const OperatorCallback &callback) const;

    /*
      Compute all abstract operators for a given concrete operator. Initialize
      data structures for initial call to recursive method multiply_out.
      variable_to_index maps variables in the task to their index in the
      pattern or -1.
    */
    void build_ranked_operators(
        const std::vector<FactPair> &preconditions,
        const std::vector<FactPair> &effects,
        const std::vector<int> &variable_to_pattern_index,
        const VariablesProxy &variables,
        const OperatorCallback &callback) const;

    /*
      Return true iff all abstract facts hold in the given state.
    */
    bool is_consistent(
        int state_index,
        const std::vector<FactPair> &abstract_facts) const;

public:
    Projection(
        const TaskProxy &task_proxy,
        const std::shared_ptr<TaskInfo> &task_info,
        const pdbs::Pattern &pattern,
        bool combine_labels = true);
    virtual ~Projection() override;

    virtual std::vector<int> compute_goal_distances(
        const std::vector<int> &operator_costs) const override;
    virtual std::vector<int> compute_saturated_costs(
        const std::vector<int> &h_values) const override;
    virtual int get_num_operators() const override;
    virtual bool operator_is_active(int op_id) const override;
    virtual bool operator_induces_self_loop(int op_id) const override;
    virtual void for_each_transition(const TransitionCallback &callback) const override;
    virtual int get_num_states() const override;
    virtual const std::vector<int> &get_goal_states() const override;

    const pdbs::Pattern &get_pattern() const;
    virtual void dump() const override;
};
}

#endif
