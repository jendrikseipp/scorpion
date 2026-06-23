#ifndef SEARCH_ALGORITHMS_ITERATIVE_DEEPENING_SEARCH_H
#define SEARCH_ALGORITHMS_ITERATIVE_DEEPENING_SEARCH_H

#include "../search_algorithm.h"

#include "../task_utils/incremental_successor_generator.h"

namespace iterative_deepening_search {
class IterativeDeepeningSearch : public SearchAlgorithm {
    const bool single_plan;
    incremental_successor_generator::IncrementalSuccessorGenerator sg;

    Plan operator_sequence;
    int last_plan_cost;

    void recursive_search(const State &state, int depth_limit);

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    IterativeDeepeningSearch(
        bool single_plan, OperatorCost cost_type, int bound, double max_time,
        const std::string &description, utils::Verbosity verbosity);

    void save_plan_if_necessary() override;

    virtual void print_statistics() const override;
};
}

#endif
