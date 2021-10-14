#ifndef CEGAR_FLAW_SEARCH
#define CEGAR_FLAW_SEARCH

#include "flaw_selector.h"

#include "../open_list.h"
#include "../search_engine.h"

namespace cegar {
class AbstractSearch;

class FlawSearch {
    const TaskProxy task_proxy;
    // std::vector<std::unique_ptr<Flaw>> flaws;
    std::unique_ptr<StateOpenList> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchStatistics> statistics;

    protected:
    virtual void initialize();
    virtual SearchStatus step();


public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task);

    void search_for_flaws(const AbstractSearch *abstract_search);
};
}

#endif
