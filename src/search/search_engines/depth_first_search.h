#ifndef SEARCH_ENGINES_DEPTH_FIRST_SEARCH_H
#define SEARCH_ENGINES_DEPTH_FIRST_SEARCH_H

#include "../search_engine.h"

#include <memory>

class Evaluator;

namespace options {
class Options;
}

namespace depth_first_search {
struct DFSNode {
    State state;
    int g;

    DFSNode(const State &state, int g)
        : state(state),
          g(g) {
    }
};


class DepthFirstSearch : public SearchEngine {
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
    explicit DepthFirstSearch(const options::Options &opts);

    virtual void save_plan_if_necessary() override;

    virtual void print_statistics() const override;
};
}

#endif
