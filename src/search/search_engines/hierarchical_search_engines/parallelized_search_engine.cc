#include "parallelized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/propositional_task.h"

#include <memory>

using namespace std;
using namespace hierarchical_search_engine;


namespace parallelized_search_engine {

ParallelizedSearchEngine::ParallelizedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) {
    m_name = "ParallelizedSearchEngine";
}

SearchStatus ParallelizedSearchEngine::step() {
    // 1. Perform one generate step of active child search.
    bool has_active_child_search = false;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        if (child_search_engine_ptr->get_is_active()) {
            has_active_child_search = true;
            child_search_engine_ptr->step();
        }
    }
    // 2. If search exhausted, notify parent.
    if (!has_active_child_search) {
        if (!m_target_state) {
            return SearchStatus::UNSOLVABLE;
        }
        for (const auto& child_search_engine_ptr : m_child_search_engines) {
            statistics.inc_expanded(child_search_engine_ptr->statistics.get_expanded());
            statistics.inc_generated(child_search_engine_ptr->statistics.get_generated());
        }
        State state = *m_target_state;
        return m_parent_search_engine->on_goal(this, state);
    }
    return SearchStatus::IN_PROGRESS;
}

SearchStatus ParallelizedSearchEngine::on_goal(HierarchicalSearchEngine* caller, const State &state)
{
    if (m_debug)
        std::cout << get_name() << " on_goal: " << m_propositional_task->compute_dlplan_state(state).str() << " " << m_plan.size() << std::endl;
    if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), state)) {
        // Achieved goal: Update current best solution.
        Plan caller_plan = caller->get_plan();
        if (!m_target_state || caller_plan.size() < m_plan.size()) {
            m_plan = caller_plan;
            m_target_state = utils::make_unique_ptr<State>(state);
            for (const auto& child_search_engine_ptr : m_child_search_engines) {
                child_search_engine_ptr->set_bound(static_cast<int>(caller_plan.size()) - 2);
            }
        }
    }
    return SearchStatus::IN_PROGRESS;
}

void ParallelizedSearchEngine::set_initial_state(const State& state) {
    HierarchicalSearchEngine::set_initial_state(state);
    m_target_state = nullptr;
}

void ParallelizedSearchEngine::print_statistics() const {
    statistics.print_detailed_statistics();
    m_search_space->print_statistics();
}


static shared_ptr<HierarchicalSearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Parallelized search engine",
        "");
    SearchEngine::add_options_to_parser(parser);
    HierarchicalSearchEngine::add_goal_test_option(parser);
    HierarchicalSearchEngine::add_child_search_engine_option(parser);

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<ParallelizedSearchEngine>(opts);
}

static Plugin<hierarchical_search_engine::HierarchicalSearchEngine> _plugin("parallelized_search", _parse);

}
