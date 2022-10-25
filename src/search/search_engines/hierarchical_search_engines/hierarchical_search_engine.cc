#include "hierarchical_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/root_task.h"

#include <deque>
#include <memory>
#include <vector>

namespace hierarchical_search_engine
{
HierarchicalSearchEngine::HierarchicalSearchEngine(
    const options::Options &opts)
    : SearchEngine(opts),
      propositional_task(nullptr),
      search_task(std::make_shared<extra_tasks::ModifiedInitialStateTask>(tasks::g_root_task, tasks::g_root_task->get_initial_state_values())),
      parent_search_engine(nullptr) {
}

void HierarchicalSearchEngine::set_propositional_task(
    std::shared_ptr<extra_tasks::PropositionalTask> propositional_task)
{
    propositional_task = propositional_task;
}

void HierarchicalSearchEngine::set_parent_search_engine(
    HierarchicalSearchEngine &parent)
{
    parent_search_engine = &parent;
}

void HierarchicalSearchEngine::on_goal(const State &state, Plan &&partial_plan)
{
    plan.insert(plan.end(), partial_plan.begin(), partial_plan.end());
    if (goal_test->is_goal(task_proxy.get_initial_state(), state)) {
        if (parent_search_engine) {
            parent_search_engine->on_goal(state, std::move(plan));
        } else {
            plan_manager.save_plan(plan, task_proxy);
        }
    }
}

void HierarchicalSearchEngine::set_initial_state(const State &state)
{
    std::vector<int> initial_state_values = state.get_unpacked_values();
    search_task = std::make_shared<extra_tasks::ModifiedInitialStateTask>(tasks::g_root_task, std::move(initial_state_values));
}

static PluginTypePlugin<HierarchicalSearchEngine> _type_plugin(
    "HierarchicalSearchEngine",
    "Hierarchical search engine.");
}
