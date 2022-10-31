#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARH_ENGINES_GOAL_CHECK_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARH_ENGINES_GOAL_CHECK_H

#include "../../task_proxy.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}


namespace goal_test {
class GoalTest {
protected:
    const std::shared_ptr<AbstractTask> task;

public:
    explicit GoalTest(const options::Options &opts);
    virtual ~GoalTest();

    virtual bool is_goal(const State& initial_state, const State& current_state) const = 0;
};

class TopGoal : public GoalTest {
public:
    explicit TopGoal(const options::Options &opts);
    bool is_goal(const State& initial_state, const State& current_state) const override;
};

class SketchSubgoal : public GoalTest {
private:
    std::string m_sketch_filename;
public:
    explicit SketchSubgoal(const options::Options &opts);
    bool is_goal(const State& initial_state, const State& current_state) const override;
};

class IncrementGoalCount : public GoalTest {
public:
    explicit IncrementGoalCount(const options::Options &opts);
    bool is_goal(const State& initial_state, const State& current_state) const override;
};



}

#endif
