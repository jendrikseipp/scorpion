#ifndef SEARCH_ALGORITHMS_DEPTH_FIRST_SEARCH_H
#define SEARCH_ALGORITHMS_DEPTH_FIRST_SEARCH_H

#include "../search_algorithm.h"

namespace depth_first_search {
struct DFSNode {
    State state;
    int g;

    DFSNode(const State &state, int g)
        : state(state),
          g(g) {
    }
};


class DepthFirstSearch : public SearchAlgorithm {
    const bool single_plan;
    int max_depth;

    utils::HashSet<State> states_on_path;
    Plan operator_sequence;
    int cheapest_plan_cost;

    void recursive_search(const DFSNode &node);
    bool check_invariants() const;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    DepthFirstSearch(
        bool single_plan, OperatorCost cost_type, int bound, double max_time,
        const std::string &description, utils::Verbosity verbosity);

    virtual void save_plan_if_necessary() override;

    virtual void print_statistics() const override;
};
}

#endif
