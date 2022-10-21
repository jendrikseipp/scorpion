#ifndef NOVELTY_STATE_MAPPER_H
#define NOVELTY_STATE_MAPPER_H

#include "fact_indexer.h"

#include "../task_proxy.h"

#include <dlplan/core.h>

#include <memory>
#include <vector>


namespace novelty {
const int UNDEFINED = -1;

class StateMapper {
private:
    FactIndexer m_fact_indexer;

    std::shared_ptr<dlplan::core::VocabularyInfo> m_vocabulary_info;
    std::shared_ptr<dlplan::core::InstanceInfo> m_instance_info;

    std::vector<int> m_fact_index_to_dlplan_atom_index;
public:
    StateMapper();
    StateMapper(const TaskProxy &task_proxy, FactIndexer fact_indexer);

    dlplan::core::State compute_dlplan_state(const State& state) const;
};

}

#endif
