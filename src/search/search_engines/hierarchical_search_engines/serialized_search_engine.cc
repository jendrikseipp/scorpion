#include "serialized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/propositional_task.h"

#include <memory>

using namespace std;
using namespace hierarchical_search_engine;


namespace serialized_search_engine {

SerializedSearchEngine::SerializedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) {
    m_name = "SerializedSearchEngine";
}

SearchStatus SerializedSearchEngine::step() {
    assert(m_child_search_engines.size() == 1);
    return m_child_search_engines.front()->step();
}

SearchStatus SerializedSearchEngine::on_goal(HierarchicalSearchEngine* caller, const State &state, Plan &&partial_plan, const SearchStatistics& child_statistics)
{
    std::cout << get_name() << " on_goal: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    m_is_active = false;
    m_plan.insert(m_plan.end(), partial_plan.begin(), partial_plan.end());
    caller->set_initial_state(state);
    if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), state)) {
        if (m_parent_search_engine) {
            // Uppropagate goal test and downpropagate global search status
            return m_parent_search_engine->on_goal(this, state, std::move(m_plan), statistics);
        } else {
            // Top-level search saves the plan when reaching top-level goal.
            plan_manager.save_plan(m_plan, task_proxy);
            return SearchStatus::SOLVED;
        }
    }
    return SearchStatus::IN_PROGRESS;
}

void SerializedSearchEngine::print_statistics() const {
    statistics.print_detailed_statistics();
    m_search_space->print_statistics();
}


static shared_ptr<HierarchicalSearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Serialized search engine",
        "");
    HierarchicalSearchEngine::add_child_search_engine_option(parser);
    HierarchicalSearchEngine::add_goal_test_option(parser);
    SearchEngine::add_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<SerializedSearchEngine>(opts);
}

// ./fast-downward.py --keep-sas-file --build=debug domain.pddl instance_2_1_0.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "serialized_search(child_searches=[iw(width=2, goal_test=increment_goal_count())], goal_test=top_goal())"
// valgrind ./downward --search "serialized_search(child_searches=[iw(width=2, goal_test=increment_goal_count())], goal_test=top_goal())" < output.sas
static Plugin<HierarchicalSearchEngine> _plugin("serialized_search", _parse);
}
