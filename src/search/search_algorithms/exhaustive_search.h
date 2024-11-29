#ifndef SEARCH_ALGORITHMS_EXHAUSTIVE_SEARCH_H
#define SEARCH_ALGORITHMS_EXHAUSTIVE_SEARCH_H

#include "../search_algorithm.h"

namespace exhaustive_search {
class ExhaustiveSearch : public SearchAlgorithm {
    int current_state_id;
    std::vector<std::vector<int>> fact_mapping;

    void dump_state(const State &state) const;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    ExhaustiveSearch();

    virtual void print_statistics() const override;
};
}

#endif
