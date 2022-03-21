#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "../search_engine.h"

#include "../utils/timer.h"

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
    std::vector<int> fact_id_offsets;
    std::vector<bool> seen_facts;
    std::vector<std::vector<bool>> seen_fact_pairs;
    utils::Timer compute_novelty_timer;

    int get_fact_id(FactPair fact) const {
        return fact_id_offsets[fact.var] + fact.value;
    }
    int get_fact_id(int var, int value) const {
        return fact_id_offsets[var] + value;
    }

    bool visit_fact_pair(int fact_id1, int fact_id2);
    bool is_novel(const State &state);
    bool is_novel(OperatorID op_id, const State &succ_state);

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
