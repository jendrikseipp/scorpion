#include "serialized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"

#include <memory>

using namespace std;
using namespace hierarchical_search_engine;


namespace serialized_search_engine {

SerializedSearchEngine::SerializedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) {
}

SearchStatus SerializedSearchEngine::step() {
    assert(m_child_search_engines.size() == 1);
    return m_child_search_engines.front()->step();
}

void SerializedSearchEngine::print_statistics() const {

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
