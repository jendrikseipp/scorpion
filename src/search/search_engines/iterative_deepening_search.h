#ifndef SEARCH_ENGINES_IDASTAR_SEARCH_H
#define SEARCH_ENGINES_IDASTAR_SEARCH_H

#include "../search_engine.h"

#include "../task_utils/marked_successor_generator.h"

#include <memory>
#include <vector>

class Evaluator;

namespace options {
class Options;
}

namespace iterative_deepening_search {
class IterativeDeepeningSearch : public SearchEngine {
    const bool single_plan;
    marked_successor_generator::MarkedSuccessorGenerator sg;

    Plan operator_sequence;
    int last_plan_cost;

    void recursive_search(const State &state, int depth_limit);

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit IterativeDeepeningSearch(const options::Options &opts);

    void save_plan_if_necessary() override;

    virtual void print_statistics() const override;
};
}

#endif
