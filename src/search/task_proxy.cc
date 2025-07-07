#include "task_proxy.h"

#include "axioms.h"
#include "state_registry.h"

#include "task_utils/causal_graph.h"
#include "task_utils/task_properties.h"

#include <iostream>

using namespace std;

StateValueReader State::get_variable_value;

State::State(const AbstractTask &task, const StateRegistry &registry,
             StateID id)
    : task(&task), registry(&registry), id(id),
      state_packer(&registry.get_state_packer()),
      num_variables(registry.get_num_variables()) {
    assert(id != StateID::no_state);
    assert(num_variables == task.get_num_variables());
}

State::State(const AbstractTask &task, const StateRegistry &registry,
             StateID id, vector<int> &&values)
    : State(task, registry, id) {
    assert(num_variables == static_cast<int>(values.size()));
    this->values = make_shared<vector<int>>(move(values));
}

State::State(const AbstractTask &task, vector<int> &&values)
    : task(&task), registry(nullptr), id(StateID::no_state), buffer(nullptr),
      values(make_shared<vector<int>>(move(values))),
      state_packer(nullptr), num_variables(this->values->size()) {
    assert(num_variables == task.get_num_variables());
}

State State::get_unregistered_successor(const OperatorProxy &op) const {
    assert(!op.is_axiom());
    assert(task_properties::is_applicable(op, *this));
    assert(values->size() != 0);
    vector<int> new_values = get_unpacked_values();

    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, *this)) {
            FactPair effect_fact = effect.get_fact().get_pair();
            new_values[effect_fact.var] = effect_fact.value;
        }
    }

    if (task->get_num_axioms() > 0) {
        AxiomEvaluator &axiom_evaluator = g_axiom_evaluators[TaskProxy(*task)];
        axiom_evaluator.evaluate(new_values);
    }
    return State(*task, move(new_values));
}

const causal_graph::CausalGraph &TaskProxy::get_causal_graph() const {
    return causal_graph::get_causal_graph(task);
}
