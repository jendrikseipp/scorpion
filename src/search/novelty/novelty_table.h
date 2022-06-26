#ifndef NOVELTY_NOVELTY_TABLE_H
#define NOVELTY_NOVELTY_TABLE_H

#include "../task_proxy.h"

#include "../utils/timer.h"

#include <cassert>
#include <memory>
#include <vector>

namespace novelty {
/* Assign indices in the following order:
    0=0: 1=0 1=1 1=2 2=0 2=1
    0=1: 1=0 1=1 1=2 2=0 2=1
    1=0: 2=0 2=1
    1=1: 2=0 2=1
    1=2: 2=0 2=1
*/
class FactIndexer {
    std::vector<int> fact_offsets;
    std::vector<int> pair_offsets;
    int num_facts;
    int num_pairs;

public:
    explicit FactIndexer(const TaskProxy &task_proxy);

    int get_fact_id(FactPair fact) const {
        return fact_offsets[fact.var] + fact.value;
    }

    int get_pair_id(FactPair fact1, FactPair fact2) const {
        assert(fact1.var != fact2.var);
        if (!(fact1 < fact2)) {
            std::swap(fact1, fact2);
        }
        assert(fact1 < fact2);
        assert(utils::in_bounds(get_fact_id(fact1), pair_offsets));
        return pair_offsets[get_fact_id(fact1)] + get_fact_id(fact2);
    }

    int get_num_facts() const {
        return num_facts;
    }

    int get_num_pairs() const {
        return num_pairs;
    }

    void dump() const;
};

class NoveltyTable {
    const int width;

    std::shared_ptr<FactIndexer> fact_indexer;
    std::vector<bool> seen_facts;
    std::vector<bool> seen_fact_pairs;

    utils::Timer compute_novelty_timer;

    void dump_state_and_novelty(const State &state, int novelty) const;

public:
    NoveltyTable(
        const TaskProxy &task_proxy,
        int width,
        const std::shared_ptr<FactIndexer> &fact_indexer = nullptr);

    static const int UNKNOWN_NOVELTY = 3;

    int compute_novelty_and_update_table(const State &state);
    int compute_novelty_and_update_table(
        const OperatorProxy &op, const State &succ_state);
    void reset();

    void print_statistics() const;
};
}

#endif
