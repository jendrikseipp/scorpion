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
    if (m_plan.size() > m_bound) {
        m_is_active = false;
        return SearchStatus::FAILED;
    }
    auto search_status = m_child_search_engines.front()->step();
    if (search_status == SearchStatus::FAILED) {
        m_is_active = false;
    }
    return search_status;
}

SearchStatus SerializedSearchEngine::on_goal(HierarchicalSearchEngine* caller, const State &state)
{
    if (m_debug)
        std::cout << get_name() << " on_goal: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    // 1. Serialize plan
    Plan caller_plan = caller->get_plan();
    m_plan.insert(m_plan.end(), caller_plan.begin(), caller_plan.end());
    statistics.inc_expanded(caller->statistics.get_expanded());
    statistics.inc_generated(caller->statistics.get_generated());
    if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), state)) {
        // Achieved goal: notify parent.
        m_is_active = false;
        if (m_parent_search_engine) {
            return m_parent_search_engine->on_goal(this, state);
        } else {
            // Top-level search saves the plan instead.
            plan_manager.save_plan(m_plan, task_proxy);
            return SearchStatus::SOLVED;
        }
    } else {
        // Unachieved goal: child search must continue from new initial state
        caller->set_initial_state(state);
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
