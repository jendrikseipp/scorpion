#ifndef SEARCH_ENGINES_PARALLELIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_PARALLELIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"

namespace options {
class Options;
}

/**
 * ParallelizedSearchEngine runs a set of searches
 * and returns the best solution.
*/
namespace hierarchical_search_engine {
class ParallelizedSearchEngine : public HierarchicalSearchEngine {
private:
    HierarchicalSearchEngine* m_active_search_engine;

    IWSearchSolutions m_partial_solutions;

protected:
    /**
     * Executes a step of each child search engine.
     * If no child search engine is active then
     * propagate goal test with best solution to parent search engine.
     */
    virtual SearchStatus step() override;

public:
    explicit ParallelizedSearchEngine(const options::Options &opts);

    virtual void reinitialize() override;

    virtual bool set_initial_state(const State& state);

    virtual void print_statistics() const override;

    virtual IWSearchSolutions get_partial_solutions() const override;
};
}

#endif
