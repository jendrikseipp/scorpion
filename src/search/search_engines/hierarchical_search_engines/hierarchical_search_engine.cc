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
      m_goal_test(opts.get<std::shared_ptr<goal_test::GoalTest>>("goal_test")),
      m_parent_search_engine(nullptr),
      m_child_search_engines(opts.get_list<std::shared_ptr<HierarchicalSearchEngine>>("child_searches")),
      m_initial_state_id(StateID::no_state),
      m_search_space(nullptr) {
}

void HierarchicalSearchEngine::initialize() {
    utils::g_log << "Top level initialization of HierarchicalSearchEngine." << endl;
    set_state_registry(std::make_shared<StateRegistry>(task_proxy));
    set_propositional_task(std::make_shared<extra_tasks::PropositionalTask>(tasks::g_root_task));
    set_initial_state(m_state_registry->get_initial_state());
    set_parent_search_engine(nullptr);
}

void HierarchicalSearchEngine::set_state_registry(std::shared_ptr<StateRegistry> state_registry) {
    m_state_registry = state_registry;
    m_search_space = utils::make_unique_ptr<SearchSpace>(*m_state_registry, utils::g_log);
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_state_registry(state_registry);
    }
}

void HierarchicalSearchEngine::set_propositional_task(
    std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) {
    m_propositional_task = propositional_task;
    m_goal_test->set_propositional_task(propositional_task);
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_propositional_task(propositional_task);
    }
}

void HierarchicalSearchEngine::set_parent_search_engine(
    HierarchicalSearchEngine* parent) {
    m_parent_search_engine = parent;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_parent_search_engine(this);
    }
}

void HierarchicalSearchEngine::set_initial_state(const State &state)
{
    m_plan.clear();
    m_initial_state_id = state.get_id();
    m_search_space = utils::make_unique_ptr<SearchSpace>(*m_state_registry, utils::g_log);
    statistics.reset();
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_initial_state(state);
    }
}

SearchStatus HierarchicalSearchEngine::on_goal(const State &state, Plan &&partial_plan, const SearchStatistics& child_statistics)
{
    statistics.inc_expanded(child_statistics.get_expanded());
    statistics.inc_evaluated_states(child_statistics.get_evaluated_states());
    statistics.inc_evaluations(child_statistics.get_evaluations());
    statistics.inc_generated(child_statistics.get_generated());
    statistics.inc_reopened(child_statistics.get_reopened());
    statistics.inc_dead_ends(child_statistics.get_deadend_states());
    m_plan.insert(m_plan.end(), partial_plan.begin(), partial_plan.end());
    for (auto child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_initial_state(state);
    }
    if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), state)) {
        if (m_parent_search_engine) {
            // Uppropagate goal test and downpropagate global search status
            return m_parent_search_engine->on_goal(state, std::move(m_plan), statistics);
        } else {
            // Top-level search saves the plan when reaching top-level goal.
            plan_manager.save_plan(m_plan, task_proxy);
            return SearchStatus::SOLVED;
        }
    }
    return SearchStatus::IN_PROGRESS;
}

SearchStatus HierarchicalSearchEngine::on_goal_leaf(const State& state) {
    // std::cout << "Initial state is now: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    Plan plan;
    m_search_space->trace_path(state, plan);
    if (m_parent_search_engine) {
        return m_parent_search_engine->on_goal(state, std::move(plan), statistics);
    } else {
        plan_manager.save_plan(plan, task_proxy);
        return SearchStatus::SOLVED;
    }
}

void HierarchicalSearchEngine::add_child_search_engine_option(OptionParser &parser) {
    parser.add_list_option<std::shared_ptr<HierarchicalSearchEngine>>(
        "child_searches",
        "The child searches to be executed.",
        "[]");
}

void HierarchicalSearchEngine::add_goal_test_option(OptionParser &parser) {
    parser.add_option<std::shared_ptr<goal_test::GoalTest>>(
        "goal_test",
        "The goal test to be executed.",
        "top_goal()");
}

static PluginTypePlugin<HierarchicalSearchEngine> _type_plugin(
    "HierarchicalSearchEngine",
    "Hierarchical search engine.");
}
