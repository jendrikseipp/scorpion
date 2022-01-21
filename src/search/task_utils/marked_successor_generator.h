#ifndef TASK_UTILS_MARKED_SUCCESSOR_GENERATOR_H
#define TASK_UTILS_MARKED_SUCCESSOR_GENERATOR_H

#include "../algorithms/array_pool.h"

#include <vector>

struct FactPair;
class OperatorID;
class State;
class TaskProxy;

namespace marked_successor_generator {
class MarkedSuccessorGenerator {
    std::vector<int> fact_id_offset;
    array_pool_template::ArrayPool<int> operators_by_precondition;
    std::vector<int> counter;
    std::vector<int> num_preconditions;
    std::vector<int> operators_without_preconditions;

    int get_fact_id(FactPair fact) const;

public:
    explicit MarkedSuccessorGenerator(const TaskProxy &task_proxy);

    void generate_applicable_ops(
        const State &state, std::vector<OperatorID> &applicable_ops);
};
}

#endif
