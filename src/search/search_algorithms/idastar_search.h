#ifndef SEARCH_ALGORITHMS_IDASTAR_SEARCH_H
#define SEARCH_ALGORITHMS_IDASTAR_SEARCH_H

#include "../search_algorithm.h"

#include "../utils/hash.h"

#include <memory>
#include <queue>

class Evaluator;

namespace idastar_search {
struct IDAstarNode {
    State state;
    int g;
    int h;

    IDAstarNode(const State &state, int g, int h)
        : state(state),
          g(g),
          h(h) {
    }
};

using CacheValue = std::pair<int, int>;
class FifoCache {
    int max_size;
    utils::HashMap<State, CacheValue> state_to_g_and_iteration;
    std::queue<State> states;

public:
    explicit FifoCache(int max_size);

    void add(const State &state, int g, int iteration);
    CacheValue lookup(const State &state) const;
    void clear();
};

class IDAstarSearch : public SearchAlgorithm {
    const std::shared_ptr<Evaluator> h_evaluator;
    const bool single_plan;

    int iteration;
    int f_limit;
    Plan operator_sequence;
    int cheapest_plan_cost;

    // Store last seen states and their g values in FIFO queue.
    std::unique_ptr<FifoCache> cache;
    int num_cache_hits;

    uint64_t num_expansions;
    uint64_t num_evaluations;

    int compute_h_value(const State &state) const;
    int recursive_search(const IDAstarNode &node);

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    IDAstarSearch(
        const std::shared_ptr<Evaluator> &h_evaluator, int initial_f_limit,
        int cache_size, bool single_plan, OperatorCost cost_type, int bound,
        double max_time, const std::string &description, utils::Verbosity verbosity);

    void save_plan_if_necessary() override;

    virtual void print_statistics() const override;
};
}

#endif
