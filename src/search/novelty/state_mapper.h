#ifndef NOVELTY_FACT_INDEXER_H
#define NOVELTY_FACT_INDEXER_H

#include "../task_proxy.h"

#include <dlplan/core.h>

#include <cassert>
#include <memory>
#include <vector>

namespace novelty {

class StateMapper {
private:
    std::shared_ptr<dlplan::core::VocabularyInfo> m_vocabulary_info;
    std::shared_ptr<dlplan::core::InstanceInfo> m_instance_info;
public:
    StateMapper();
    ~StateMapper();

    dlplan::core::State compute_dlplan_state(const State& state) const;
};

}

#endif
