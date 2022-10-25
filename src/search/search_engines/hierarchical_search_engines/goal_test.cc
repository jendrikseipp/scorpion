#include "goal_test.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../task_utils/task_properties.h"

#include <memory>

using namespace std;


namespace goal_test {

GoalTest::GoalTest(const options::Options &opts) { }
GoalTest::~GoalTest() { }

TopGoal::TopGoal(const options::Options &opts)
    : GoalTest(opts) { }
bool TopGoal::is_goal(const State& initial_state, const State& current_state) const { }

SketchSubgoal::SketchSubgoal(const options::Options &opts)
    : GoalTest(opts) { }
bool SketchSubgoal::is_goal(const State& initial_state, const State& current_state) const { }

IncrementGoalCount::IncrementGoalCount(const options::Options &opts)
    : GoalTest(opts) { }
bool IncrementGoalCount::is_goal(const State& initial_state, const State& current_state) const {
    /*if (task_properties::is_goal_state(task_proxy, state)) {
        log << "Solution found!" << endl;
        Plan plan;
        search_space.trace_path(state, plan);
        set_plan(plan);
        return true;
    }
    return false;*/
}

static shared_ptr<GoalTest> _parse_top_goal(OptionParser &parser) {
    parser.document_synopsis(
        "Top goal test",
        "");
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<TopGoal>(opts);
}

static shared_ptr<GoalTest> _parse_sketch_subgoal(OptionParser &parser) {
    parser.document_synopsis(
        "Sketch subgoal test",
        "");
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<SketchSubgoal>(opts);
}

static shared_ptr<GoalTest> _parse_increment_goal_count(OptionParser &parser) {
    parser.document_synopsis(
        "Increment goal count test",
        "");
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<IncrementGoalCount>(opts);
}

static PluginTypePlugin<GoalTest> _type_plugin(
    "GoalTest",
    "Goal test for novelty based search.");
static Plugin<GoalTest> _plugin_top_goal("top_goal", _parse_top_goal);
static Plugin<GoalTest> _plugin_sketch_subgoal("sketch_subgoal", _parse_sketch_subgoal);
static Plugin<GoalTest> _plugin_increment_goal_count("increment_goal_count", _parse_increment_goal_count);
}
