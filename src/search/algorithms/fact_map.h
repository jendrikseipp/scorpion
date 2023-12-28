#ifndef ALGORITHMS_FACT_MAP_H
#define ALGORITHMS_FACT_MAP_H

#include "../abstract_task.h"

#include <vector>

class TaskProxy;

namespace fact_map {
class FactMap {
    std::vector<int> fact_offsets;
    std::vector<int> values;

    int get_fact_id(FactPair fact) const {
        return fact_offsets[fact.var] + fact.value;
    }

public:
    FactMap(const std::vector<int> &domain_sizes, int default_value);

    int operator[](FactPair fact) const {
        return values[get_fact_id(fact)];
    }

    int &operator[](FactPair fact) {
        return values[get_fact_id(fact)];
    }

    int size() const {
        return values.size();
    }
};
}

#endif
