#ifndef SEARCH_ENGINES_BREADTH_FIRST_SEARCH_H
#define SEARCH_ENGINES_BREADTH_FIRST_SEARCH_H

#include "../search_engine.h"

#include <memory>
#include <vector>

class PruningMethod;

namespace options {
class Options;
}

namespace breadth_first_search {
struct Parent {
    StateID state_id;
    OperatorID op_id;

    Parent()
        : state_id(StateID::no_state),
          op_id(OperatorID::no_operator) {
    }

    Parent(StateID state_id, OperatorID op_id)
        : state_id(state_id), op_id(op_id) {
    }
};

/* NOTE:
    Doesn't support reach_state
    Doesn't support bound
    Doesn't produce log lines for new g values
    Doesn't generate a plan file for solvable tasks
*/
class BreadthFirstSearch : public SearchEngine {
    const bool write_plan;
    int current_state_id;
    PerStateInformation<Parent> parents;
    std::shared_ptr<PruningMethod> pruning_method;

    std::vector<OperatorID> trace_path(const State &goal_state) const;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit BreadthFirstSearch(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif
