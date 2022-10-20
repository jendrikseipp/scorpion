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

    std::vector<int> m_fact_index_to_dlplan_atom_index;
public:
    /**
     * Maps task proxy information to the information from the given file
     * such that we can map State to dlplan::core::State.
     */
    StateMapper(
        const TaskProxy &task_proxy,
        const std::string& atoms_filename,
        const std::string& predicates_filename,
        const std::string& static_atoms_filename,
        const std::string& goal_atoms_filename);

    dlplan::core::State compute_dlplan_state(const State& state) const;
};

}

#endif
