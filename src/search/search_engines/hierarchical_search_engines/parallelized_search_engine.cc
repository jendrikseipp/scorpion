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
      m_shortest_plan_size(std::numeric_limits<int>::max()) {
    m_name = "ParallelizedSearchEngine";
}

SearchStatus ParallelizedSearchEngine::step() {
    // case 1: perform steps of active child searches in parallel.
    bool has_active_child_search = false;
    SearchStatus combined_status = SearchStatus::UNSOLVABLE;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        if (child_search_engine_ptr->get_is_active()) {
            has_active_child_search = true;
            SearchStatus child_search_status = child_search_engine_ptr->step();
            switch (child_search_status) {
                case SearchStatus::SOLVED: {
                    return child_search_status;
                }
                case SearchStatus::IN_PROGRESS: {
                    combined_status = SearchStatus::IN_PROGRESS;
                    break;
                }
                default:
                    break;
            }
        }
    }
    if (has_active_child_search) {
        return combined_status;
    } else {
        // case 2: propagate goal check
        if (m_partial_plans.empty()) {
            return SearchStatus::UNSOLVABLE;
        }
        size_t best_search_index = -1;
        int bound = std::numeric_limits<int>::max();
        for (size_t i = 0; i < m_partial_plans.size(); ++i) {
            if (m_partial_plans.size() < bound) {
                best_search_index = bound;
                bound = m_partial_plans.size();
            }
        }
        for (const auto& child_search_engine_ptr : m_child_search_engines) {
            child_search_engine_ptr->set_initial_state(m_target_states[best_search_index]);
        }
        return m_parent_search_engine->on_goal(this, m_target_states[best_search_index], std::move(m_partial_plans[best_search_index]), statistics);
    }
    return SearchStatus::IN_PROGRESS;
}

SearchStatus ParallelizedSearchEngine::on_goal(HierarchicalSearchEngine* caller, const State &state, Plan &&partial_plan, const SearchStatistics& child_statistics)
{
    std::cout << get_name() << " on_goal: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    m_shortest_plan_size = std::min<int>(m_shortest_plan_size, partial_plan.size());
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        child_search_engine_ptr->set_bound(m_shortest_plan_size - 2);
    }
    m_partial_plans.push_back(std::move(partial_plan));
    m_target_states.push_back(state);
    return SearchStatus::IN_PROGRESS;
}

void ParallelizedSearchEngine::set_initial_state(const State& state) {
    HierarchicalSearchEngine::set_initial_state(state);
    m_partial_plans.clear();
    m_target_states.clear();
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
