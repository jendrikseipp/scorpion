#include "serialized_search_engine.h"

#include "../../option_parser.h"
#include "../../plugin.h"

#include <memory>

using namespace std;

namespace serialized_search_engine {

SerializedSearchEngine::SerializedSearchEngine(const options::Options &opts)
    : hierarchical_search_engine::HierarchicalSearchEngine(opts) { }

SearchStatus SerializedSearchEngine::step() {
    assert(m_child_search_engines.size() == 1);
    return m_child_search_engines.front()->step();
}

void SerializedSearchEngine::print_statistics() const {

}


static shared_ptr<SerializedSearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Serialized search engine",
        "");
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<SerializedSearchEngine>(opts);
}

static Plugin<hierarchical_search_engine::HierarchicalSearchEngine> _plugin("serialized_search", _parse);

}
