#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H

#include "goal_test.h"

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
    std::string m_name;

    std::shared_ptr<StateRegistry> m_state_registry;
    std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task;
    std::shared_ptr<goal_test::GoalTest> m_goal_test;

    /* Parent-child relationship:
       Every HierarchicalSearchEngine has a parent and a collection of childs.
    */
    HierarchicalSearchEngine* m_parent_search_engine;
    std::vector<std::shared_ptr<HierarchicalSearchEngine>> m_child_search_engines;

    StateID m_initial_state_id;
    std::unique_ptr<SearchSpace> m_search_space;
    Plan m_plan;

protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);

    /**
     * Top-level initialization.
     */
    virtual void initialize() override;

    /**
     * React upon reaching goal state.
     * Propagate goal test up in the hierarchy.
     * Propagate global search status down in the hierarchy.
     */
    virtual SearchStatus on_goal(HierarchicalSearchEngine* caller, const State &state, Plan&& partial_plan, const SearchStatistics& statistics);

    /**
     * React upon reaching goal state in leaf search engine.
     */
    virtual SearchStatus on_goal_leaf(const State& state);

    /**
     * Setters used to (re-)initialize child searches
     */
    virtual void set_state_registry(std::shared_ptr<StateRegistry> state_registry);
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
    virtual void set_parent_search_engine(HierarchicalSearchEngine* parent);
    virtual void set_initial_state(const State& state);

public:
    static void add_child_search_engine_option(options::OptionParser &parser);
    static void add_goal_test_option(options::OptionParser &parser);
};
}

#endif
