#ifndef NOVELTY_FACT_INDEXER_H
#define NOVELTY_FACT_INDEXER_H

#include "../task_proxy.h"

#include "../utils/timer.h"

#include <cassert>
#include <memory>
#include <vector>

namespace novelty {
class FactIndexer {
    std::vector<int> fact_offsets;
    int num_facts;
    int num_pairs;

public:
    explicit FactIndexer(const TaskProxy &task_proxy);

    /**
     * Consider all state facts.
     */
    std::vector<int> get_fact_ids(const State& state) const {
        std::vector<int> fact_ids;
        int num_vars = state.size();
        fact_ids.reserve(num_vars);
        for (FactProxy fact_proxy : state) {
            FactPair fact = fact_proxy.get_pair();
            int fact_id = get_fact_id(fact);
            fact_ids.push_back(fact_id);
        }
        return fact_ids;
    }

    /**
     * Consider only state facts over variables that were affected by operator.
     */
    std::vector<int> get_fact_ids(const OperatorProxy &op, const State& state) const {
        std::vector<int> fact_ids;
        int num_vars = state.size();
        for (EffectProxy effect : op.get_effects()) {
            FactPair fact = effect.get_fact().get_pair();
            for (int var2 = 0; var2 < num_vars; ++var2) {
                if (fact.var == var2) {
                    continue;
                }
                int fact_id = get_fact_id(fact);
                fact_ids.push_back(fact_id);
            }
        }
        return fact_ids;
    }

    int get_fact_id(FactPair fact) const {
        return fact_offsets[fact.var] + fact.value;
    }

    int get_num_facts() const {
        return num_facts;
    }

    void dump() const;
};

}

#endif
