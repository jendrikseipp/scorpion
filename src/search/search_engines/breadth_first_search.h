#ifndef SEARCH_ENGINES_BREADTH_FIRST_SEARCH_H
#define SEARCH_ENGINES_BREADTH_FIRST_SEARCH_H

#include "../search_engine.h"

#include <memory>

class PruningMethod;

namespace options {
class Options;
}

namespace breadth_first_search {
/* NOTE:
    Doesn't support reach_state
    Doesn't support bound
    Doesn't produce log lines for new g values
    Doesn't generate a plan file for solvable tasks
*/
class BreadthFirstSearch : public SearchEngine {
    int current_state_id;
    std::shared_ptr<PruningMethod> pruning_method;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit BreadthFirstSearch(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif
