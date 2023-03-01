#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H

#include "goal_test.h"

#include "../../search_engine.h"
#include "../../state_id.h"

#include <memory>
#include <vector>


namespace options {
class Options;
class OptionParser;
}

namespace hierarchical_search_engine {
class ParallelizedSearchEngine;
class SerializedSearchEngine;

/**
 * Solution of an IW search.
 * In addition to plan, we also store
 * the target state to proceed search greedily,
 * the effective width used to solve the subproblem.
*/
struct IWSearchSolution {
    // The applied actions
    Plan plan;
    // The reached state
    StateID state_id;
    // effective width;
    int ew;

    IWSearchSolution() :
        state_id(StateID::no_state) { }

    IWSearchSolution(Plan plan, StateID state_id, int ew)
        : plan(plan), state_id(state_id), ew(ew) { }
};

using IWSearchSolutions = std::vector<IWSearchSolution>;


class HierarchicalSearchEngine : public SearchEngine {
friend class SerializedSearchEngine;
friend class ParallelizedSearchEngine;
friend class IWSearch;

protected:
    std::string m_name;

    std::shared_ptr<StateRegistry> m_state_registry;
    std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task;
    std::shared_ptr<goal_test::GoalTest> m_goal_test;

    HierarchicalSearchEngine* m_parent_search_engine;
    std::vector<std::shared_ptr<HierarchicalSearchEngine>> m_child_search_engines;

    // maximum bound until search terminates
    int m_bound;

    StateID m_initial_state_id;

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
    virtual void reinitialize();

    virtual bool is_goal(const State &state);

    /**
     * Child-level initialization.
     */
    virtual void set_state_registry(std::shared_ptr<StateRegistry> state_registry);
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
    virtual void set_parent_search_engine(HierarchicalSearchEngine* parent);

    /**
     * Setters.
     * Returns true iff search engine provides additional subgoal states.
     */
    virtual bool set_initial_state(const State& state);
    virtual void set_bound(int bound);

    /**
     * Getters.
     */
    virtual std::string get_name();
    virtual IWSearchSolutions get_partial_solutions() const = 0;
    virtual SearchStatistics collect_statistics() const;

    static int compute_partial_solutions_length(const IWSearchSolutions& partial_solutions);

public:
    static void add_child_search_engine_option(options::OptionParser &parser);
    static void add_goal_test_option(options::OptionParser &parser);

    virtual void search() override;
};
}

#endif
