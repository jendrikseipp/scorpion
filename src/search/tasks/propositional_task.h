#ifndef TASKS_PROPOSITIONAL_TASK_H
#define TASKS_PROPOSITIONAL_TASK_H

#include "delegating_task.h"

#include "../task_proxy.h"

#include <dlplan/core.h>

#include <memory>
#include <vector>


namespace extra_tasks {
const int UNDEFINED = -1;

class PropositionalTask : public tasks::DelegatingTask {
private:
    const std::vector<int> initial_state_values;

    std::shared_ptr<dlplan::core::VocabularyInfo> vocabulary_info;
    std::shared_ptr<dlplan::core::InstanceInfo> instance_info;

    std::vector<int> fact_offsets;
    int num_facts;
    std::vector<int> fact_index_to_dlplan_atom_index;

public:
    PropositionalTask(const std::shared_ptr<AbstractTask> &parent);

    virtual std::vector<int> get_initial_state_values() const override;

    std::vector<int> get_fact_ids(const State& state) const;
    // TODO: does not work yet.
    std::vector<int> get_fact_ids(const OperatorProxy &op, const State& state) const;

    int get_fact_id(FactPair fact) const;
    int get_num_facts() const;

    dlplan::core::State compute_dlplan_state(const State& state) const;
};
}

#endif
