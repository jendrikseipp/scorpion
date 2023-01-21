#include "parallelized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/propositional_task.h"

#include <memory>

using namespace std;
using namespace hierarchical_search_engine;


namespace parallelized_search_engine {

ParallelizedSearchEngine::ParallelizedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts),
    m_in_progress_child_searches(m_child_search_engines.size(), true) {
    m_name = "ParallelizedSearchEngine";
}

SearchStatus ParallelizedSearchEngine::step() {
    for (size_t i = 0; i < m_child_search_engines.size(); ++i) {
        bool in_progress = m_in_progress_child_searches[i];
        if (!in_progress) continue;

        // 1. Execute one expansion step in parallel
        HierarchicalSearchEngine& child_search = *m_child_search_engines[i].get();
        SearchStatus search_status = child_search.step();
        if (search_status != SearchStatus::IN_PROGRESS) {
            m_in_progress_child_searches[i] = false;
        }

        if (search_status == SearchStatus::SOLVED) {
            // 2. Update current best solution
            //    and solution upper bound in child searches
            Plan child_plan = child_search.get_plan();
            if (!m_target_state || child_plan.size() < m_plan.size()) {
                m_plan = child_plan;
                m_target_state = utils::make_unique_ptr<State>(m_state_registry->lookup_state(child_search.m_goal_state_id));
                for (const auto& child_search_engine_ptr : m_child_search_engines) {
                    child_search_engine_ptr->set_bound(static_cast<int>(m_plan.size()) - 2);
                }
            }
        }
    }
    // 3. Search finished: return resulting search status and update statistics.
    bool all_not_in_progress = std::all_of(m_in_progress_child_searches.begin(), m_in_progress_child_searches.end(), [](bool x){ return !x; });
    if (all_not_in_progress) {
        for (const auto& child_search_engine_ptr : m_child_search_engines) {
            statistics.inc_expanded(child_search_engine_ptr->statistics.get_expanded());
            statistics.inc_generated(child_search_engine_ptr->statistics.get_generated());
        }

        if (!m_target_state) {
            return SearchStatus::UNSOLVABLE;
        }
        if (static_cast<int>(m_plan.size()) > m_bound) {
            return SearchStatus::FAILED;
        }

        if (m_debug)
            std::cout << get_name() << " on_goal: " << m_propositional_task->compute_dlplan_state(*m_target_state).str() << " " << m_plan.size() << std::endl;
        return SearchStatus::SOLVED;
    }
    return SearchStatus::IN_PROGRESS;
}

void ParallelizedSearchEngine::set_initial_state(const State& state) {
    HierarchicalSearchEngine::set_initial_state(state);
    m_target_state = nullptr;
    std::fill(m_in_progress_child_searches.begin(), m_in_progress_child_searches.end(), true);
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
