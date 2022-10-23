#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "../search_engine.h"

#include "../novelty/fact_indexer.h"
#include "../novelty/state_mapper.h"
#include <dlplan/novelty.h>

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace hierarchical_search_engine {
class HierarchicalSearchEngine : public SearchEngine {
private:
    SearchEngine* parent_search_engine;

protected:
    virtual bool check_goal_and_set_plan(const HierarchicalSearchEngine& parent, const State &state) = 0;

    virtual void set_parent_search_engine(SearchEngine& parent) = 0;

    virtual void set_initial_state(const State& state) = 0;
public:
    /**
     * task must be ModifiedInitialStateTask
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);
};
}

#endif
