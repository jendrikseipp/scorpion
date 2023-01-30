#include "serialized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/propositional_task.h"

#include <memory>

using namespace std;


namespace hierarchical_search_engine {

SerializedSearchEngine::SerializedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) {
    m_name = "SerializedSearchEngine";
    if (m_child_search_engines.size() != 1) {
        throw std::runtime_error("SerializedSearchEngine::SerializedSearchEngine - exactly one child search engine required.");
    }
}

SearchStatus SerializedSearchEngine::step() {
    HierarchicalSearchEngine& child_search = *m_child_search_engines.front().get();
    SearchStatus search_status = child_search.step();
    if (search_status == SearchStatus::SOLVED) {
        // 1. Concatenate partial plan
        IWSearchSolutions child_partial_solutions = child_search.get_partial_solutions();
        m_partial_solutions.insert(m_partial_solutions.end(), child_partial_solutions.begin(), child_partial_solutions.end());
        int length = compute_partial_solutions_length(m_partial_solutions);
        if (length > m_bound) {
            return SearchStatus::FAILED;
        } else if (is_goal(m_state_registry->lookup_state(m_partial_solutions.back().state_id))) {
            // 2. Search finished: return resulting search status and update statistics.
            if (m_debug)
                std::cout << get_name() << " goal_state: " << m_propositional_task->compute_dlplan_state(m_state_registry->lookup_state(m_partial_solutions.back().state_id)).str() << std::endl;
            return SearchStatus::SOLVED;
        } else {
            // 3. Search unfinished: update child search initial states
            m_child_search_engines.front()->reinitialize();
            m_child_search_engines.front()->set_initial_state(m_state_registry->lookup_state(m_partial_solutions.back().state_id));
            return SearchStatus::IN_PROGRESS;
        }
    }
    return search_status;
}

void SerializedSearchEngine::reinitialize() {
    HierarchicalSearchEngine::reinitialize();
    m_partial_solutions.clear();
}

void SerializedSearchEngine::print_statistics() const {
    statistics.print_detailed_statistics();
}

IWSearchSolutions SerializedSearchEngine::get_partial_solutions() const {
    return m_partial_solutions;
}


static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
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

// ./fast-downward.py --keep-sas-file ../sketch-learner/testing/benchmarks/delivery/domain.pddl ../sketch-learner/testing/benchmarks/delivery/instance_3_3_0.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "serialized_search(child_searches=[iw(width=2, goal_test=increment_goal_count())], goal_test=top_goal())"
static Plugin<SearchEngine> _plugin("serialized_search", _parse);
}
