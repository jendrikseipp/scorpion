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
    std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task;

public:
    explicit GoalTest(const options::Options &opts);
    virtual ~GoalTest();

    virtual bool set_initial_state(const State& initial_state);
    virtual bool is_goal(const State& current_state) const = 0;

    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
};

class TopGoal : public GoalTest {
public:
    explicit TopGoal(const options::Options &opts);
    bool is_goal(const State& current_state) const override;
};

class SketchSubgoal : public GoalTest {
private:
    std::string m_sketch_filename;
    dlplan::policy::Policy m_policy;
    dlplan::core::State m_initial_state;
    std::vector<std::shared_ptr<const dlplan::policy::Rule>> m_satisfied_rules;

public:
    explicit SketchSubgoal(const options::Options &opts);
    bool set_initial_state(const State& initial_state) override;
    bool is_goal(const State& current_state) const override;

    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) override;
};

class IncrementGoalCount : public GoalTest {
private:
    int m_num_unsatisfied_initial_goal_facts;

private:
    int compute_num_unsatisfied_goal_facts(const State& state) const;

public:
    explicit IncrementGoalCount(const options::Options &opts);
    bool set_initial_state(const State& initial_state) override;
    bool is_goal(const State& current_state) const override;
};



}

#endif
