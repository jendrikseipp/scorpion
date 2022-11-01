#include "hierarchical_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/root_task.h"
#include "../../tasks/propositional_task.h"

#include <string>
#include <deque>
#include <memory>
#include <vector>

using namespace std;


namespace hierarchical_search_engine {
HierarchicalSearchEngine::HierarchicalSearchEngine(
    const options::Options &opts)
    : SearchEngine(opts),
      m_state_registry(nullptr),
      m_propositional_task(nullptr),
      m_search_task(std::make_shared<extra_tasks::ModifiedInitialStateTask>(tasks::g_root_task, tasks::g_root_task->get_initial_state_values())),
      m_goal_test(opts.get<std::shared_ptr<goal_test::GoalTest>>("goal_test")),
      m_parent_search_engine(nullptr) {
}

void HierarchicalSearchEngine::initialize() {
    utils::g_log << "Top level initialization of HierarchicalSearchEngine." << endl;
    set_state_registry(std::make_shared<StateRegistry>(task_proxy));
    set_propositional_task(std::make_shared<extra_tasks::PropositionalTask>(tasks::g_root_task));
    set_initial_state(state_registry.get_initial_state());
}

void HierarchicalSearchEngine::set_state_registry(std::shared_ptr<StateRegistry> state_registry) {
    m_state_registry = state_registry;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_state_registry(state_registry);
    }
}

void HierarchicalSearchEngine::set_propositional_task(
    std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) {
    m_propositional_task = propositional_task;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_propositional_task(propositional_task);
    }
}

void HierarchicalSearchEngine::set_parent_search_engine(
    HierarchicalSearchEngine &parent) {
    m_parent_search_engine = &parent;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_parent_search_engine(*this);
    }
}

void HierarchicalSearchEngine::on_goal(const State &state, Plan &&partial_plan)
{
    m_plan.insert(m_plan.end(), partial_plan.begin(), partial_plan.end());
    if (m_goal_test->is_goal(task_proxy.get_initial_state(), state)) {
        if (m_parent_search_engine) {
            m_parent_search_engine->on_goal(state, std::move(m_plan));
        } else {
            plan_manager.save_plan(m_plan, task_proxy);
        }
    }
}

bool HierarchicalSearchEngine::check_goal_and_set_plan(const State& initial_state, const State& target_state) {
    if (m_goal_test->is_goal(initial_state, target_state)) {
        Plan plan;
        search_space.trace_path(target_state, plan);
        m_parent_search_engine->on_goal(target_state, std::move(plan));
        return true;
    }
    return false;
}

void HierarchicalSearchEngine::set_initial_state(const State &state)
{
    std::vector<int> initial_state_values = state.get_unpacked_values();
    m_search_task = std::make_shared<extra_tasks::ModifiedInitialStateTask>(tasks::g_root_task, std::move(initial_state_values));
}

void HierarchicalSearchEngine::add_child_search_engine_option(OptionParser &parser) {
    parser.add_list_option<std::shared_ptr<HierarchicalSearchEngine>>(
        "child_searches",
        "The child searches to be executed.",
        "");
}

void HierarchicalSearchEngine::add_goal_test_option(OptionParser &parser) {
    parser.add_list_option<std::shared_ptr<goal_test::GoalTest>>(
        "goal_test",
        "The goal test to be executed.",
        "top_goal()");
}

static PluginTypePlugin<HierarchicalSearchEngine> _type_plugin(
    "HierarchicalSearchEngine",
    "Hierarchical search engine.");
}
