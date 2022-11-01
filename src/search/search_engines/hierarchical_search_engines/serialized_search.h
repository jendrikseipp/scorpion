#ifndef SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"
#include "goal_test.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace serialized_search_engine {
class SerializedSearchEngine : public hierarchical_search_engine::HierarchicalSearchEngine {
protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit SerializedSearchEngine(const options::Options &opts);
};
}

#endif
