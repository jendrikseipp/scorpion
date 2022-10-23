#ifndef SEARCH_ENGINES_SERIALIZED_ITERATIVE_WIDTH_SKETCH_SEARCH_H
#define SEARCH_ENGINES_SERIALIZED_ITERATIVE_WIDTH_SKETCH_SEARCH_H

#include "../search_engine.h"

#include "../novelty/fact_indexer.h"
#include "../novelty/state_mapper.h"
#include <dlplan/novelty.h>

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace siwr_search {
class SIWRSearch : public SearchEngine {
    const int width;
    const bool debug;

    std::deque<StateID> open_list;
    std::deque<StateID> closed_list;

    StateID m_initial_state_id;
    std::shared_ptr<dlplan::novelty::NoveltyBase> m_novelty_base;
    dlplan::novelty::NoveltyTable m_novelty_table;
    novelty::FactIndexer m_fact_indexer;
    novelty::StateMapper m_state_mapper;

    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy &op, const State &succ_state);

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit SIWRSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
