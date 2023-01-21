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
friend class iw_search::IWSearch;

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
    StateID m_goal_state_id;
    std::unique_ptr<SearchSpace> m_search_space;
    Plan m_plan;

    // maximum bound until search terminates
    int m_bound;

    bool m_debug;

protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);

    /**
     * Top-level initialization.
     */
    virtual void initialize() override;
    virtual bool initial_state_goal_test() override;

    /**
     * Child-level initialization.
     */
    virtual void set_state_registry(std::shared_ptr<StateRegistry> state_registry);
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
    virtual void set_parent_search_engine(HierarchicalSearchEngine* parent);

    /**
     * Setters.
     */
    virtual void set_initial_state(const State& state);
    virtual void set_bound(int bound);

    /**
     * Getters.
     */
    virtual std::string get_name();
    virtual Plan get_plan();

public:
    static void add_child_search_engine_option(options::OptionParser &parser);
    static void add_goal_test_option(options::OptionParser &parser);
};
}

#endif
