#ifndef SEARCH_ENGINES_SERIALIZED_SEARCH_H
#define SEARCH_ENGINES_SERIALIZED_SEARCH_H

#include "../search_engine.h"

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace serialized_search {
class SerializedSearch : public SearchEngine {
protected:
    virtual void initialize() override;
    /**
     * Each step runs a search algorithm for one step.
     * If subgoal is reached then reinitialize search algorithm
     * for new initial state and concatenate plan.
     * If topgoal is reached return solution.
     */
    virtual SearchStatus step() override;

public:
    explicit SerializedSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
