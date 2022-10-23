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
protected:
    virtual bool check_goal_and_set_plan(const HierarchicalSearchEngine& parent, const State &state) = 0;

public:
    explicit HierarchicalSearchEngine(const options::Options &opts);
};
}

#endif
