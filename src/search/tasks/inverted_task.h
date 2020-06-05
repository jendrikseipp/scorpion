#ifndef TASKS_INVERTED_TASK_H
#define TASKS_INVERTED_TASK_H

#include "delegating_task.h"

#include "../utils/collections.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace extra_tasks {
struct InvertedOperator {
    std::vector<FactPair> preconditions;
    std::vector<FactPair> effects;
    int parent_operator_id;
    int cost;
};

class InvertedTask : public tasks::DelegatingTask {
    std::vector<InvertedOperator> operators;

public:
    InvertedTask(
        const std::shared_ptr<AbstractTask> &parent,
        std::vector<InvertedOperator> &&inverted_operators);

    virtual int get_operator_cost(int index, bool is_axiom) const override;
    virtual std::string get_operator_name(int index, bool is_axiom) const override;
    virtual int get_num_operators() const override;
    virtual int get_num_operator_preconditions(int index, bool is_axiom) const override;
    virtual FactPair get_operator_precondition(
        int op_index, int fact_index, bool is_axiom) const override;
    virtual int get_num_operator_effects(int op_index, bool is_axiom) const override;
    virtual int get_num_operator_effect_conditions(
        int op_index, int eff_index, bool is_axiom) const override;
    virtual FactPair get_operator_effect_condition(
        int op_index, int eff_index, int cond_index, bool is_axiom) const override;
    virtual FactPair get_operator_effect(
        int op_index, int eff_index, bool is_axiom) const override;
    virtual int convert_operator_index_to_parent(int index) const override {
        return operators[index].parent_operator_id;
    }

    virtual FactPair get_goal_fact(int index) const override;
    virtual std::vector<int> get_initial_state_values() const override;
};
}

#endif
