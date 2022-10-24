#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H

#include "goal_test.h"

#include "../../tasks/modified_initial_state_task.h"
#include "../../search_engine.h"

#include "../../novelty/fact_indexer.h"
#include "../../novelty/state_mapper.h"
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
    std::shared_ptr<goal_test::GoalTest> goal_test;
    std::shared_ptr<extra_tasks::ModifiedInitialStateTask> modified_task;
    HierarchicalSearchEngine* parent_search_engine;

    Plan plan;

protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);

    /**
     * Sets the parent_search_engine.
     */
    virtual void initialize(HierarchicalSearchEngine& parent);

    /**
     * React upon reaching goal state.
     */
    virtual void on_goal(const State &state, Plan&& partial_plan);

    /**
     * Sets the initial state of ModifiedInitialStateTask, resets SearchSpace, and reset partial plan.
     */
    virtual void set_initial_state(const State& state);
};
}

#endif
