#ifndef SEARCH_ENGINES_PARALLELIZED_SEARCH_ENGINE_H
#define SEARCH_ENGINES_PARALLELIZED_SEARCH_ENGINE_H

#include "hierarchical_search_engine.h"


namespace options {
class Options;
}

namespace parallelized_search_engine {
class ParallelizedSearchEngine : public hierarchical_search_engine::HierarchicalSearchEngine {
private:
    std::vector<Plan> m_partial_plans;
    std::vector<State> m_target_states;
    int m_shortest_plan_size;

protected:
    virtual SearchStatus step() override;

public:
    explicit ParallelizedSearchEngine(const options::Options &opts);

    virtual SearchStatus on_goal(HierarchicalSearchEngine* caller, const State &state, Plan&& partial_plan, const SearchStatistics& statistics) override;

    virtual void set_initial_state(const State& state);

    virtual void print_statistics() const override;
};
}

#endif
