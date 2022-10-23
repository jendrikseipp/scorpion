#ifndef NOVELTY_GOAL_CHECK_H
#define NOVELTY_GOAL_CHECK_H

#include "fact_indexer.h"

#include "../task_proxy.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}


namespace novelty {
class GoalTest {
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
