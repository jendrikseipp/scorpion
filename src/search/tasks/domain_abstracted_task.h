#ifndef TASKS_DOMAIN_ABSTRACTED_TASK_H
#define TASKS_DOMAIN_ABSTRACTED_TASK_H

#include "delegating_task.h"

#include "../algorithms/array_pool.h"
#include "../utils/collections.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace extra_tasks {
struct AbstractedVariable {
    int var;
    int pool_index;
};

class ValueMap {
    std::vector<AbstractedVariable> abstracted_variables;
    std::vector<int> variable_to_pool_index;
    array_pool_template::ArrayPool<int> new_values;

public:
    ValueMap(
        const AbstractTask &task,
        const AbstractTask &parent_task,
        std::vector<std::vector<int>> &&value_map);
    void convert(std::vector<int> &state_values) const;
    FactPair convert(const FactPair &fact) const;
    bool does_convert_values() const;
};

/*
  Task transformation for performing domain abstraction.

  We recommend using the factory function in
  domain_abstracted_task_factory.h for creating DomainAbstractedTasks.
*/
class DomainAbstractedTask : public tasks::DelegatingTask {
    const std::vector<int> domain_size;
    const std::vector<int> initial_state_values;
    const std::vector<FactPair> goals;
    const std::vector<std::vector<std::string>> fact_names;
    const ValueMap value_map;

public:
    DomainAbstractedTask(
        const std::shared_ptr<AbstractTask> &parent,
        std::vector<int> &&domain_size,
        std::vector<int> &&initial_state_values,
        std::vector<FactPair> &&goals,
        std::vector<std::vector<std::string>> &&fact_names,
        std::vector<std::vector<int>> &&value_map);

    virtual int get_variable_domain_size(int var) const override;
    virtual std::string get_fact_name(const FactPair &fact) const override;
    virtual bool are_facts_mutex(
        const FactPair &fact1, const FactPair &fact2) const override;

    virtual FactPair get_operator_precondition(
        int op_index, int fact_index, bool is_axiom) const override;
    virtual FactPair get_operator_effect(
        int op_index, int eff_index, bool is_axiom) const override;

    virtual FactPair get_goal_fact(int index) const override;

    virtual std::vector<int> get_initial_state_values() const override;
    virtual void convert_state_values_from_parent(
        std::vector<int> &values) const override;
    virtual bool does_convert_ancestor_state_values(
        const AbstractTask *ancestor_task) const override;
};
}

#endif
