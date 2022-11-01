#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARH_ENGINES_GOAL_CHECK_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARH_ENGINES_GOAL_CHECK_H

#include "../../task_proxy.h"

#include <dlplan/policy.h>

#include <memory>
#include <vector>

namespace options {
class Options;
}


namespace goal_test {
class GoalTest {
protected:
    const std::shared_ptr<AbstractTask> m_task;

    std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task;

public:
    explicit GoalTest(const options::Options &opts);
    virtual ~GoalTest();

    virtual bool is_goal(const State& initial_state, const State& current_state) const = 0;

    /**
     * Setters.
     */
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
};

class TopGoal : public GoalTest {
public:
    explicit TopGoal(const options::Options &opts);
    bool is_goal(const State& initial_state, const State& current_state) const override;
};

class SketchSubgoal : public GoalTest {
private:
    std::string m_sketch_filename;
    dlplan::policy::Policy m_policy;

public:
    explicit SketchSubgoal(const options::Options &opts);
    bool is_goal(const State& initial_state, const State& current_state) const override;

    /**
     *
     */
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) override;
};

class IncrementGoalCount : public GoalTest {
public:
    explicit IncrementGoalCount(const options::Options &opts);
    bool is_goal(const State& initial_state, const State& current_state) const override;
};



}

#endif
