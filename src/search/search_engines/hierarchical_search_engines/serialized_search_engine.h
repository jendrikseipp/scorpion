#ifndef SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_SERIALIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"


namespace options {
class Options;
}

namespace serialized_search_engine {
class SerializedSearchEngine : public hierarchical_search_engine::HierarchicalSearchEngine {
protected:
    virtual SearchStatus step() override;

public:
    explicit SerializedSearchEngine(const options::Options &opts);

    virtual SearchStatus on_goal(HierarchicalSearchEngine* caller, const State &state) override;

    virtual void print_statistics() const override;
};
}

#endif
