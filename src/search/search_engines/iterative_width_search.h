#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "../search_engine.h"

#include "../novelty/novelty_table.h"

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace iterative_width_search {
class IterativeWidthSearch : public SearchEngine {
    const int width;
    const bool debug;

    std::deque<StateID> open_list;
    novelty::NoveltyTable novelty_table;

    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy &op, const State &succ_state);

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit IterativeWidthSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
