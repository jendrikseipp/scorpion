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
    if (m_child_search_engines.size() != 1) {
        throw std::runtime_error("SerializedSearchEngine::SerializedSearchEngine - exactly one child search engine required.");
    }
}

SearchStatus SerializedSearchEngine::step() {
    auto& child_search = *m_child_search_engines.front().get();
    auto search_status = child_search.step();
    if (search_status == SearchStatus::SOLVED) {
        // 1. Concatenate partial plan
        State goal_state = m_state_registry->lookup_state(child_search.m_goal_state_id);
        Plan child_plan = child_search.get_plan();
        m_plan.insert(m_plan.end(), child_plan.begin(), child_plan.end());
        if (static_cast<int>(m_plan.size()) > m_bound) {
            statistics.inc_expanded(child_search.statistics.get_expanded());
            statistics.inc_generated(child_search.statistics.get_generated());
            return SearchStatus::FAILED;
        } else if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), goal_state)) {
            // 2. Search finished: return resulting search status and update statistics.
            statistics.inc_expanded(child_search.statistics.get_expanded());
            statistics.inc_generated(child_search.statistics.get_generated());
            if (m_debug)
                std::cout << get_name() << " goal_state: " << m_propositional_task->compute_dlplan_state(goal_state).str() << std::endl;
            if (!m_parent_search_engine) {
                plan_manager.save_plan(m_plan, task_proxy);
            }
            return SearchStatus::SOLVED;
        } else {
            // 3. Search unfinished: update child search initial states
            m_child_search_engines.front()->set_initial_state(goal_state);
            return SearchStatus::IN_PROGRESS;
        }
    } else if (search_status == SearchStatus::FAILED) {
        statistics.inc_expanded(child_search.statistics.get_expanded());
        statistics.inc_generated(child_search.statistics.get_generated());
    }
    return search_status;
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
