#include "marked_successor_generator.h"

#include "task_properties.h"

#include "../utils/logging.h"

using namespace std;

namespace marked_successor_generator {
static array_pool_template::ArrayPool<FactPair> get_effects_by_operator(
    const OperatorsProxy &ops) {
    array_pool_template::ArrayPool<FactPair> effects_by_operator;
    int total_num_effects = 0;
    for (OperatorProxy op : ops) {
        total_num_effects += op.get_effects().size();
    }
    effects_by_operator.reserve(ops.size(), total_num_effects);
    for (OperatorProxy op : ops) {
        vector<FactPair> effects;
        effects.reserve(op.get_effects().size());
        for (EffectProxy effect : op.get_effects()) {
            effects.push_back(effect.get_fact().get_pair());
        }
        sort(effects.begin(), effects.end());
        effects_by_operator.push_back(move(effects));
    }
    return effects_by_operator;
}

MarkedSuccessorGenerator::MarkedSuccessorGenerator(const TaskProxy &task_proxy)
    : effects_by_operator(get_effects_by_operator(task_proxy.get_operators())) {
    utils::Timer init_timer;
    int num_operators = task_proxy.get_operators().size();
    int num_facts = 0;
    for (VariableProxy var : task_proxy.get_variables()) {
        fact_id_offset.push_back(num_facts);
        num_facts += var.get_domain_size();
    }

    counter.resize(num_operators, 0);
    num_preconditions.reserve(num_operators);

    vector<vector<int>> precondition_of(num_facts);
    for (OperatorProxy op : task_proxy.get_operators()) {
        if (op.get_preconditions().empty()) {
            operators_without_preconditions.push_back(op.get_id());
        }
        for (FactProxy precondition : op.get_preconditions()) {
            precondition_of[get_fact_id(precondition.get_pair())].push_back(op.get_id());
        }
        num_preconditions.push_back(op.get_preconditions().size());
    }
    for (vector<int> &op_ids : precondition_of) {
        operators_by_precondition.push_back(move(op_ids));
    }

    utils::g_log << "Time for initializing marked successor generator: " << init_timer << endl;
}

void MarkedSuccessorGenerator::reset_to_state(const State &state) {
    applicable_operators.clear();
    applicable_operators_position = vector<int>(num_preconditions.size(), -1);
    for (int op_id : operators_without_preconditions) {
        applicable_operators_position[op_id] = applicable_operators.size();
        applicable_operators.push_back(op_id);
    }
    counter = num_preconditions;
    for (FactProxy fact : state) {
        for (int op_id : operators_by_precondition[get_fact_id(fact.get_pair())]) {
            --counter[op_id];
            assert(counter[op_id] >= 0);
            if (counter[op_id] == 0) {
                applicable_operators_position[op_id] = applicable_operators.size();
                applicable_operators.push_back(op_id);
            }
        }
    }
}

void MarkedSuccessorGenerator::insert_op(int op) {
    assert(applicable_operators_position[op] == -1);
    assert(find(applicable_operators.begin(), applicable_operators.end(), op)
           == applicable_operators.end());
    applicable_operators_position[op] = applicable_operators.size();
    applicable_operators.push_back(op);
}

void MarkedSuccessorGenerator::erase_op(int op) {
    int op_pos = applicable_operators_position[op];
    assert(op_pos != -1);
    assert(!applicable_operators.empty());
    int last_op = applicable_operators.back();
    int last_op_pos = applicable_operators_position[last_op];
    assert(last_op_pos != -1);
    assert(last_op_pos = static_cast<int>(applicable_operators.size() - 1));
    swap(applicable_operators[op_pos], applicable_operators[last_op_pos]);
    swap(applicable_operators_position[op], applicable_operators_position[last_op]);
    assert(applicable_operators.back() == op);
    applicable_operators.pop_back();
    applicable_operators_position[op] = -1;
}

void MarkedSuccessorGenerator::push_transition(const State &state, int op_id) {
    for (FactPair new_fact : effects_by_operator[op_id]) {
        FactPair old_fact = state[new_fact.var].get_pair();
        if (new_fact == old_fact) {
            continue;
        }
        for (int op : operators_by_precondition[get_fact_id(old_fact)]) {
            if (counter[op] == 0) {
                erase_op(op);
            }
            ++counter[op];
        }
        for (int op : operators_by_precondition[get_fact_id(new_fact)]) {
            --counter[op];
            if (counter[op] == 0) {
                insert_op(op);
            }
        }
    }
}

void MarkedSuccessorGenerator::pop_transition(const State &src, int op_id) {
    for (FactPair new_fact : effects_by_operator[op_id]) {
        FactPair old_fact = src[new_fact.var].get_pair();
        if (new_fact == old_fact) {
            continue;
        }
        for (int op : operators_by_precondition[get_fact_id(new_fact)]) {
            if (counter[op] == 0) {
                erase_op(op);
            }
            ++counter[op];
        }
        for (int op : operators_by_precondition[get_fact_id(old_fact)]) {
            --counter[op];
            if (counter[op] == 0) {
                insert_op(op);
            }
        }
    }
}

const std::vector<int> &MarkedSuccessorGenerator::get_applicable_operators() {
    return applicable_operators;
}

int MarkedSuccessorGenerator::get_fact_id(FactPair fact) const {
    return fact_id_offset[fact.var] + fact.value;
}
}
