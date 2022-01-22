#ifndef TASK_UTILS_INCREMENTAL_SUCCESSOR_GENERATOR_H
#define TASK_UTILS_INCREMENTAL_SUCCESSOR_GENERATOR_H

#include "../algorithms/array_pool.h"

#include <unordered_set>
#include <vector>

struct FactPair;
class OperatorID;
class State;
class TaskProxy;

namespace incremental_successor_generator {
class IncrementalSuccessorGenerator {
    // These members are logically const.
    array_pool_template::ArrayPool<FactPair> effects_by_operator;
    std::vector<int> fact_id_offset;
    array_pool_template::ArrayPool<int> operators_by_precondition;
    std::vector<int> num_preconditions;
    std::vector<int> operators_without_preconditions;

    std::vector<int> counter;
    std::vector<int> applicable_operators_position;
    std::vector<int> applicable_operators;

    int get_fact_id(FactPair fact) const;
    void insert_op(int op);
    void erase_op(int op);

public:
    explicit IncrementalSuccessorGenerator(const TaskProxy &task_proxy);

    void reset_to_state(const State &state);
    void push_transition(const State &state, int op_id);
    void pop_transition(const State &src, int op_id);
    const std::vector<int> &get_applicable_operators();
};
}

#endif
