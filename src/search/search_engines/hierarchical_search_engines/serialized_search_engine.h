#ifndef SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"

using namespace hierarchical_search_engine;

namespace options {
class Options;
}

namespace serialized_search_engine {
class SerializedSearchEngine : public HierarchicalSearchEngine {
private:
    IWSearchSolutions m_partial_solutions;

protected:
    /**
     * Executes a step of the single child search engine.
     */
    virtual SearchStatus step() override;

public:
    explicit SerializedSearchEngine(const options::Options &opts);

    virtual void print_statistics() const override;

    virtual IWSearchSolutions get_partial_solutions() const override;
};
}

#endif
