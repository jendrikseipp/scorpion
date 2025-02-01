#ifndef SEARCH_ALGORITHMS_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ALGORITHMS_ITERATIVE_WIDTH_SEARCH_H

#include "../search_algorithm.h"

#include "../novelty/novelty_table.h"

#include <deque>

// TODO: rename to IteratedWidthSearch.
namespace iterative_width_search {
class IterativeWidthSearch : public SearchAlgorithm {
    std::deque<StateID> open_list;
    novelty::NoveltyTable novelty_table;

    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy &op, const State &succ_state);

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    IterativeWidthSearch(
        int width, OperatorCost cost_type, int bound, double max_time,
        const std::string &description, utils::Verbosity verbosity);

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
