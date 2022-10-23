#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "../tasks/modified_initial_state_task.h"
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
/**
 * A HierarchicalSearchEngine has at least one child HierarchicalSearchEngine.
 * Each step runs a step of the low level search.
 * If the low level search reaches a goal it notifies its parent
 */
class HierarchicalSearchEngine : public SearchEngine {
private:
    const std::shared_ptr<extra_tasks::ModifiedInitialStateTask> modified_task;
    SearchEngine* parent_search_engine;

protected:
    /**
     * Sets the parent_search_engine.
     */
    virtual void set_parent_search_engine(SearchEngine& parent);

    /**
     * Get notified about successful goal test and partial plan from child search.
     */
    virtual bool on_child_achieves_goal(const State &state, const Plan& partial_plan) = 0;

    /**
     * Sets the initial state of ModifiedInitialStateTask, resets SearchSpace, and reset partial plan.
     */
    virtual void set_initial_state(const State& state) = 0;
public:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);
};
}

#endif
