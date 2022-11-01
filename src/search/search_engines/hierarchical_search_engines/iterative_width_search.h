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
    std::deque<StateID> closed_list;

    StateID m_initial_state_id;
    std::shared_ptr<dlplan::novelty::NoveltyBase> m_novelty_base;
    dlplan::novelty::NoveltyTable m_novelty_table;

    std::unique_ptr<goal_test::GoalTest> goal_test;

private:
    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy &op, const State &succ_state);

protected:
    virtual SearchStatus step() override;

    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) override;
    virtual void set_initial_state(const State& state) override;

public:
    explicit IWSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif
