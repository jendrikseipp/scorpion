#ifndef TASKS_PROPOSITIONAL_TASK_H
#define TASKS_PROPOSITIONAL_TASK_H

#include "delegating_task.h"

#include "../task_proxy.h"

#include <dlplan/core.h>

#include <memory>
#include <vector>

namespace novelty {
class FactIndexer;
}

namespace extra_tasks {
const int UNDEFINED = -1;

class PropositionalTask : public tasks::DelegatingTask {
private:
    const std::vector<int> m_initial_state_values;

    std::shared_ptr<dlplan::core::VocabularyInfo> m_vocabulary_info;
    std::shared_ptr<dlplan::core::InstanceInfo> m_instance_info;
    dlplan::core::SyntacticElementFactory m_syntactic_element_factory;
    dlplan::core::DenotationsCaches m_denotations_caches;

    std::vector<int> fact_offsets;
    int num_facts;
    std::vector<int> fact_index_to_dlplan_atom_index;

    std::unordered_set<int> m_goal_facts;

    std::shared_ptr<novelty::FactIndexer> m_fact_indexer;

public:
    PropositionalTask(const std::shared_ptr<AbstractTask> &parent);

    dlplan::core::State compute_dlplan_state(const State& state) const;

    virtual std::vector<int> get_initial_state_values() const override;

    std::vector<int> get_fact_ids(const State& state) const;
    // TODO: does not work yet.
    std::vector<int> get_fact_ids(const OperatorProxy &op, const State& state) const;

    int get_fact_id(FactPair fact) const;
    int get_num_facts() const;

    const std::unordered_set<int>& get_goal_facts() const;

    dlplan::core::SyntacticElementFactory& get_syntactic_element_factory_ref();
    dlplan::core::DenotationsCaches& get_denotations_caches();

    std::shared_ptr<novelty::FactIndexer> get_fact_indexer();
};
}

#endif
