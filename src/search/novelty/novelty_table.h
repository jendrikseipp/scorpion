#ifndef NOVELTY_NOVELTY_TABLE_H
#define NOVELTY_NOVELTY_TABLE_H

#include "../task_proxy.h"

#include "../algorithms/array_pool.h"

#include <cassert>
#include <vector>

namespace novelty {
/* Assign indices in the following order:
    0=0: 1=0 1=1 1=2 2=0 2=1
    0=1: 1=0 1=1 1=2 2=0 2=1
    1=0: 2=0 2=1
    1=1: 2=0 2=1
    1=2: 2=0 2=1
*/
class TaskInfo {
    std::vector<int> primary_variables;
    array_pool_template::ArrayPool<FactPair> effects_by_operator;
    std::vector<int> fact_offsets;
    std::vector<int64_t> pair_offsets;
    bool has_axioms;
    int num_facts;
    int64_t num_pairs;

public:
    explicit TaskInfo(const TaskProxy &task_proxy);

    const std::vector<int> &get_primary_variables() const {
        return primary_variables;
    }

    array_pool_template::ArrayPoolSlice<FactPair> get_effects(int op_id) const {
        return effects_by_operator[op_id];
    }

    int64_t get_fact_id(FactPair fact) const {
        return fact_offsets[fact.var] + fact.value;
    }

    int64_t get_pair_id(FactPair fact1, FactPair fact2) const {
        assert(fact1.var != fact2.var);
        if (!(fact1 < fact2)) {
            std::swap(fact1, fact2);
        }
        assert(fact1 < fact2);
        assert(utils::in_bounds(static_cast<long>(get_fact_id(fact1)), pair_offsets));
        return pair_offsets[get_fact_id(fact1)] + get_fact_id(fact2);
    }

    int get_num_facts() const {
        return num_facts;
    }

    int64_t get_num_pairs() const {
        return num_pairs;
    }

    void dump() const;
};

class NoveltyTable {
    int width;

    const TaskInfo &task_info;
    std::vector<bool> seen_facts;
    std::vector<bool> seen_fact_pairs;

public:
    NoveltyTable(int width, const TaskInfo &task_info);

    static const int UNKNOWN_NOVELTY = 3;

    int compute_novelty_and_update_table(const State &state);
    int compute_novelty_and_update_table(
        const State &parent_state, int op_id, const State &succ_state);
    void reset();
    void dump();
};
}

#endif
