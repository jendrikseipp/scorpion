#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "hierarchical_search_engine.h"

#include <dlplan/novelty.h>

#include <deque>
#include <memory>
#include <vector>

using namespace hierarchical_search_engine;

namespace options {
class Options;
}

namespace iw_search {
class IWSearch : public HierarchicalSearchEngine {
    const int width;
    const bool iterate;
    const bool debug;

    std::deque<StateID> open_list;
    int m_current_width;

    std::shared_ptr<dlplan::novelty::NoveltyBase> m_novelty_base;
    dlplan::novelty::NoveltyTable m_novelty_table;

    std::unique_ptr<SearchSpace> m_search_space;

    IWSearchSolution m_solution;

private:
    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy &op, const State &succ_state);

protected:
    /**
     * Generates next successor state and reacts upon.
     */
    virtual SearchStatus step() override;

    virtual void set_state_registry(std::shared_ptr<StateRegistry> state_registry) override;
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) override;
    virtual void set_initial_state(const State& state) override;

public:
    explicit IWSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;

    virtual IWSearchSolutions get_partial_solutions() const override;
};
}

#endif
