#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H

#include "goal_test.h"

#include "../../tasks/modified_initial_state_task.h"
#include "../../search_engine.h"

#include <memory>
#include <vector>


namespace options {
class Options;
class OptionParser;
}

namespace serialized_search_engine {
class SerializedSearchEngine;
}

namespace parallelized_search_engine {
class ParallelizedSearchEngine;
}

namespace hierarchical_search_engine {
class HierarchicalSearchEngine : public SearchEngine {
friend class serialized_search_engine::SerializedSearchEngine;
friend class parallelized_search_engine::ParallelizedSearchEngine;

protected:
    std::shared_ptr<StateRegistry> m_state_registry;
    std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task;
    std::shared_ptr<extra_tasks::ModifiedInitialStateTask> m_search_task;
    std::shared_ptr<goal_test::GoalTest> m_goal_test;

    /* Parent-child relationship:
       Every HierarchicalSearchEngine has a parent and a collection of childs.
    */
    HierarchicalSearchEngine* m_parent_search_engine;
    std::vector<std::shared_ptr<hierarchical_search_engine::HierarchicalSearchEngine>> m_child_search_engines;

    Plan m_plan;

protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);

    /**
     * Top-level initialization.
     * HierarchicalSearchEngines that contain children must initialize children
     */
    virtual void initialize() override;

    /**
     * React upon reaching goal state.
     */
    virtual void on_goal(const State &state, Plan&& partial_plan);

    virtual bool check_goal_and_set_plan(const State& initial_state, const State& target_state);

    /**
     * Setters
     */
    virtual void set_state_registry(std::shared_ptr<StateRegistry> state_registry);
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
    virtual void set_parent_search_engine(HierarchicalSearchEngine& parent);
    virtual void set_initial_state(const State& state);

public:
    static void add_child_search_engine_option(options::OptionParser &parser);
    static void add_goal_test_option(options::OptionParser &parser);
};
}

#endif
