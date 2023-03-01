#include "goal_test.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/root_task.h"
#include "../../task_utils/task_properties.h"
#include "../../tasks/propositional_task.h"

#include <memory>
#include <fstream>
#include <sstream>

using namespace std;


namespace goal_test {

GoalTest::GoalTest(const options::Options&) { }

GoalTest::~GoalTest() { }

bool GoalTest::set_initial_state(const State&) {
    return false;
}

void GoalTest::set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) {
    m_propositional_task = propositional_task;
}


TopGoal::TopGoal(const options::Options &opts)
    : GoalTest(opts) { }

bool TopGoal::is_goal(const State& current_state) const {
    return task_properties::is_goal_state(TaskProxy(*tasks::g_root_task), current_state);
}


SketchSubgoal::SketchSubgoal(const options::Options &opts)
    : GoalTest(opts),
      m_sketch_filename(opts.get<std::string>("filename")),
      m_initial_state(dlplan::core::State(nullptr, std::vector<int>({}))) { }

bool SketchSubgoal::set_initial_state(const State& initial_state) {
    m_initial_state = m_propositional_task->compute_dlplan_state(initial_state);
    m_satisfied_rules = m_policy.evaluate_conditions_eager(m_initial_state, m_propositional_task->get_denotations_caches());
    return m_satisfied_rules.size() > 0;
}

bool SketchSubgoal::is_goal(const State& current_state) const {
    bool is_subgoal = m_policy.evaluate_effects_lazy(
        m_initial_state,
        m_propositional_task->compute_dlplan_state(current_state),
        m_satisfied_rules,
        m_propositional_task->get_denotations_caches()) != nullptr;
    bool is_topgoal = task_properties::is_goal_state(TaskProxy(*tasks::g_root_task), current_state);
    return is_subgoal || is_topgoal;
}

void SketchSubgoal::set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) {
    GoalTest::set_propositional_task(propositional_task);
    std::ifstream infile(m_sketch_filename);
    std::stringstream content;
    content << infile.rdbuf();
    std::cout << m_sketch_filename << std::endl;
    std::cout << content.str() << std::endl;
    m_policy = dlplan::policy::PolicyReader().read(content.str(), propositional_task->get_syntactic_element_factory_ref());
}


IncrementGoalCount::IncrementGoalCount(const options::Options &opts)
    : GoalTest(opts), m_num_unsatisfied_initial_goal_facts(-1) { }

bool IncrementGoalCount::set_initial_state(const State& initial_state) {
    m_num_unsatisfied_initial_goal_facts = compute_num_unsatisfied_goal_facts(initial_state);
    return true;
}

bool IncrementGoalCount::is_goal(const State& current_state) const {
    return m_num_unsatisfied_initial_goal_facts > compute_num_unsatisfied_goal_facts(current_state);
}

int IncrementGoalCount::compute_num_unsatisfied_goal_facts(const State& state) const {
    int unsatisfied_goal_facts = m_propositional_task->get_goal_fact_ids().size();
    for (int fact_id : m_propositional_task->get_state_fact_ids(state)) {
        if (m_propositional_task->get_goal_fact_ids().count(fact_id)) {
            --unsatisfied_goal_facts;
        }
    }
    return unsatisfied_goal_facts;
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
    parser.add_option<std::string>("filename", "filename to sketch", "");
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
