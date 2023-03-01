#include "parallelized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/propositional_task.h"

#include <memory>

using namespace std;


namespace hierarchical_search_engine {

ParallelizedSearchEngine::ParallelizedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) {
    m_name = "ParallelizedSearchEngine";
}

SearchStatus ParallelizedSearchEngine::step() {
    assert(m_active_search_engine);
    SearchStatus search_status = m_active_search_engine->step();
    if (search_status == SearchStatus::SOLVED) {
        IWSearchSolutions child_partial_solutions = m_active_search_engine->get_partial_solutions();
        m_partial_solutions.insert(m_partial_solutions.end(), child_partial_solutions.begin(), child_partial_solutions.end());
        State subgoal_state = m_state_registry->lookup_state(child_partial_solutions.back().state_id);
        if (is_goal(subgoal_state)) {
            return SearchStatus::SOLVED;
        } else {
            set_initial_state(subgoal_state);
        }
        m_active_search_engine->reinitialize();
    }
    return SearchStatus::IN_PROGRESS;
}

void ParallelizedSearchEngine::reinitialize() {
    HierarchicalSearchEngine::reinitialize();
    m_partial_solutions.clear();
}

bool ParallelizedSearchEngine::set_initial_state(const State& state) {
    if (m_debug)
        std::cout << get_name() << " set_initial_state: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;

    m_goal_test->set_initial_state(state);
    m_initial_state_id = state.get_id();
    m_active_search_engine = nullptr;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
        if (child_search_engine_ptr->set_initial_state(state)) {
            m_active_search_engine = child_search_engine_ptr.get();
        }
    }
    return m_active_search_engine != nullptr;
}

void ParallelizedSearchEngine::print_statistics() const {
}

IWSearchSolutions ParallelizedSearchEngine::get_partial_solutions() const {
    return m_partial_solutions;
}


static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
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

static Plugin<SearchEngine> _plugin("parallelized_search", _parse);

}
