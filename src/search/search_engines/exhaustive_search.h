#ifndef SEARCH_ENGINES_EXHAUSTIVE_SEARCH_H
#define SEARCH_ENGINES_EXHAUSTIVE_SEARCH_H

#include "../search_engine.h"

namespace options {
class Options;
}

namespace exhaustive_search {
class ExhaustiveSearch : public SearchEngine {
    int current_state_id;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit ExhaustiveSearch(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif
