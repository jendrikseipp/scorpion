#ifndef SEARCH_ENGINES_SERIALIZED_SEARCH_H
#define SEARCH_ENGINES_SERIALIZED_SEARCH_H

#include "../search_engine.h"

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace parallelized_search {
class ParallelizedSearch : public SearchEngine {
protected:
    virtual void initialize() override;
    /**
     * Each step runs a set of search algorithms for one step.
     * If one search finds a subgoal then reinitialize initial
     * states of all search algorithms and return solution.
     */
    virtual SearchStatus step() override;

public:
    explicit ParallelizedSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
