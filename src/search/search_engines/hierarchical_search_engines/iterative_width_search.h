#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "hierarchical_search_engine.h"

#include <dlplan/novelty.h>

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace iw_search {
class IWSearch : public hierarchical_search_engine::HierarchicalSearchEngine {
    const int width;
    const bool debug;

    std::deque<StateID> open_list;
    StateID m_current_state_id;
    std::unique_ptr<SearchNode> m_current_search_node;
    std::vector<OperatorID> m_applicable_ops;
    size_t m_current_op;

    std::shared_ptr<dlplan::novelty::NoveltyBase> m_novelty_base;
    dlplan::novelty::NoveltyTable m_novelty_table;

private:
    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy &op, const State &succ_state);

protected:
    /**
     * Generates next successor state and reacts upon.
     */
    virtual SearchStatus step() override;

    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) override;
    virtual void set_initial_state(const State& state) override;

public:
    explicit IWSearch(const options::Options &opts);

    virtual SearchStatus on_goal(HierarchicalSearchEngine* caller, const State& state) override;

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
