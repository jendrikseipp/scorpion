#ifndef TASKS_INVERTED_TASK_H
#define TASKS_INVERTED_TASK_H

#include "delegating_task.h"

namespace extra_tasks {
struct InvertedOperator {
    // Postconditions in forward direction.
    std::vector<FactPair> preconditions;
    // Preconditions in forward direction.
    std::vector<FactPair> effects;

    InvertedOperator(
        std::vector<FactPair> &&preconditions,
        std::vector<FactPair> &&effects)
        : preconditions(move(preconditions)),
          effects(move(effects)) {
    }
};

class InvertedTask : public tasks::DelegatingTask {
    std::vector<InvertedOperator> operators;

public:
    explicit InvertedTask(const std::shared_ptr<AbstractTask> &parent);

    virtual int get_num_operator_preconditions(int index, bool is_axiom) const override;
    virtual FactPair get_operator_precondition(
        int op_index, int fact_index, bool is_axiom) const override;
    virtual int get_num_operator_effects(int op_index, bool is_axiom) const override;
    virtual FactPair get_operator_effect(
        int op_index, int eff_index, bool is_axiom) const override;

    virtual FactPair get_goal_fact(int index) const override;
    virtual std::vector<int> get_initial_state_values() const override;
};
}

#endif
