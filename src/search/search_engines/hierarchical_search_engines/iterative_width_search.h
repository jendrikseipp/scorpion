#ifndef SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H
#define SEARCH_ENGINES_ITERATIVE_WIDTH_SEARCH_H

#include "hierarchical_search_engine.h"

#include "../../novelty/novelty_table.h"

#include <dlplan/novelty.h>

#include <deque>
#include <memory>
#include <vector>


namespace options {
class Options;
}

namespace hierarchical_search_engine {
class IWSearch : public HierarchicalSearchEngine {
    const int m_width;
    const bool m_iterate;

    std::deque<StateID> m_open_list;
    int m_current_width;

    novelty::NoveltyTable m_novelty_table;

    std::unique_ptr<SearchSpace> m_search_space;

    IWSearchSolution m_solution;

private:
    bool is_novel(const State &state);
    bool is_novel(const OperatorProxy& op, const State &state);

protected:
    virtual void reinitialize() override;

    /**
     * Generates next successor state and reacts upon.
     * Returns:
     *    - SOLVED if solution is found and can be retrieved with get_partial_solutions()
     *    - FAILED if either search terminated by bound of search space explored
     *    - IN_PROGRESS if search unfinished
     */
    virtual SearchStatus step() override;

    virtual void set_state_registry(std::shared_ptr<StateRegistry> state_registry) override;
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task) override;
    virtual bool set_initial_state(const State& state) override;

    virtual SearchStatistics collect_statistics() const override;

public:
    explicit IWSearch(const options::Options &opts);

    virtual void print_statistics() const override;

    void dump_search_space() const;

    virtual IWSearchSolutions get_partial_solutions() const override;
};
}

#endif
