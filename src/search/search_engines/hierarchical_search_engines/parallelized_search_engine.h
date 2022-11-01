#ifndef SEARCH_ENGINES_PARALLELIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_PARALLELIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"


namespace options {
class Options;
}

namespace parallelized_search_engine {
class ParallelizedSearchEngine : public hierarchical_search_engine::HierarchicalSearchEngine {
protected:
    virtual SearchStatus step() override;

public:
    explicit ParallelizedSearchEngine(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif
