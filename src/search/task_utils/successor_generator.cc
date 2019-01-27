#include "successor_generator.h"

#include "successor_generator_factory.h"
#include "successor_generator_internals.h"

#include "../abstract_task.h"
#include "../global_state.h"

using namespace std;

namespace successor_generator {
SuccessorGenerator::SuccessorGenerator(const TaskProxy &task_proxy)
    : root(SuccessorGeneratorFactory().create(task_proxy)) {
}

SuccessorGenerator::SuccessorGenerator(
    const vector<int> &domain_sizes,
    vector<vector<FactPair>> &&preconditions)
    : root(SuccessorGeneratorFactory().create(domain_sizes, move(preconditions))) {
}

SuccessorGenerator::~SuccessorGenerator() = default;

void SuccessorGenerator::generate_applicable_ops(
    const State &state, vector<OperatorID> &applicable_ops) const {
    root->generate_applicable_ops(state, applicable_ops);
}

void SuccessorGenerator::generate_applicable_ops(
    const GlobalState &state, vector<OperatorID> &applicable_ops) const {
    root->generate_applicable_ops(state, applicable_ops);
}

PerTaskInformation<SuccessorGenerator> g_successor_generators;
}
