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
namespace parallelized_search_engine {
class ParallelizedSearchEngine : public hierarchical_search_engine::HierarchicalSearchEngine {
private:
    // the target state reached by shortest plan
    std::unique_ptr<State> m_target_state;

    std::vector<bool> m_in_progress_child_searches;

protected:
    /**
     * Executes a step of each child search engine.
     * If no child search engine is active then
     * propagate goal test with best solution to parent search engine.
     */
    virtual SearchStatus step() override;

public:
    explicit ParallelizedSearchEngine(const options::Options &opts);

    virtual void set_initial_state(const State& state);

    virtual void print_statistics() const override;
};
}

#endif
