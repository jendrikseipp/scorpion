#ifndef SEARCH_ALGORITHMS_BREADTH_FIRST_SEARCH_H
#define SEARCH_ALGORITHMS_BREADTH_FIRST_SEARCH_H

#include "../search_algorithm.h"

#include <memory>
#include <vector>

class PruningMethod;

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
    Doesn't support reach_state.
    Doesn't support bound.
    Doesn't produce log lines for new g values.
*/
class BreadthFirstSearch : public SearchAlgorithm {
    const bool single_plan;
    const bool write_plan;
    int last_plan_cost;
    int current_state_id;
    PerStateInformation<Parent> parents;
    std::shared_ptr<PruningMethod> pruning_method;

    std::vector<OperatorID> trace_path(const State &goal_state) const;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    BreadthFirstSearch(
        bool single_plan, bool write_plan, const std::shared_ptr<PruningMethod> &pruning,
        const std::string &description, utils::Verbosity verbosity);

    virtual void save_plan_if_necessary() override;

    virtual void print_statistics() const override;
};
}

#endif
