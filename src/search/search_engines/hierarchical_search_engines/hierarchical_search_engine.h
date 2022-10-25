#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H

#include "goal_test.h"

#include "../../tasks/modified_initial_state_task.h"
#include "../../search_engine.h"

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace hierarchical_search_engine {
/**
 */
class HierarchicalSearchEngine : public SearchEngine {
protected:
    std::shared_ptr<extra_tasks::PropositionalTask> propositional_task;
    std::shared_ptr<extra_tasks::ModifiedInitialStateTask> search_task;

    std::shared_ptr<goal_test::GoalTest> goal_test;
    HierarchicalSearchEngine* parent_search_engine;

    Plan plan;

protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);

    /**
     * React upon reaching goal state.
     */
    virtual void on_goal(const State &state, Plan&& partial_plan);

    /**
     * Setters
     */
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
    virtual void set_parent_search_engine(HierarchicalSearchEngine& parent);
    virtual void set_initial_state(const State& state);
};
}

#endif
