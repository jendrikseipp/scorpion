#include "parallelized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"

#include <memory>

using namespace std;
using namespace hierarchical_search_engine;


namespace parallelized_search_engine {

ParallelizedSearchEngine::ParallelizedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) { }

SearchStatus ParallelizedSearchEngine::step() {
    SearchStatus combined_status = SearchStatus::UNSOLVABLE;
    for (const auto& child_search_engine_ptr : m_child_search_engines) {
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
    return combined_status;
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
