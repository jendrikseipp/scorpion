#include "marked_successor_generator.h"

#include "task_properties.h"

#include "../utils/logging.h"

using namespace std;

namespace marked_successor_generator {
MarkedSuccessorGenerator::MarkedSuccessorGenerator(const TaskProxy &task_proxy) {
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

void MarkedSuccessorGenerator::generate_applicable_ops(
    const State &state, vector<OperatorID> &applicable_ops) {
    for (int op_id : operators_without_preconditions) {
        applicable_ops.emplace_back(op_id);
    }
    counter = num_preconditions;
    for (FactProxy fact : state) {
        for (int op_id : operators_by_precondition[get_fact_id(fact.get_pair())]) {
            --counter[op_id];
            assert(counter[op_id] >= 0);
            if (counter[op_id] == 0) {
                applicable_ops.push_back(OperatorID(op_id));
            }
        }
    }
}

int MarkedSuccessorGenerator::get_fact_id(FactPair fact) const {
    return fact_id_offset[fact.var] + fact.value;
}
}
