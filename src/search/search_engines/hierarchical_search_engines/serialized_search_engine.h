#ifndef SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"


namespace options {
class Options;
}

namespace serialized_search_engine {
class SerializedSearchEngine : public hierarchical_search_engine::HierarchicalSearchEngine {
protected:
    /**
     * Executes a step of the single child search engine.
     */
    virtual SearchStatus step() override;

public:
    explicit SerializedSearchEngine(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif
