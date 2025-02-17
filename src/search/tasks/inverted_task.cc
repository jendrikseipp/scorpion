#include "inverted_task.h"

#include "../task_proxy.h"

#include "../task_utils/task_properties.h"
#include "../utils/system.h"

#include <map>

using namespace std;

namespace extra_tasks {
static vector<FactPair> get_postconditions(
    const OperatorProxy &op) {
    // Use map to obtain sorted postconditions.
    map<int, int> var_to_post;
    for (FactProxy fact : op.get_preconditions()) {
        var_to_post[fact.get_variable().get_id()] = fact.get_value();
    }
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        var_to_post[fact.var] = fact.value;
    }
    vector<FactPair> postconditions;
    postconditions.reserve(var_to_post.size());
    for (const pair<const int, int> &fact : var_to_post) {
        postconditions.emplace_back(fact.first, fact.second);
    }
    return postconditions;
}

static vector<InvertedOperator> compute_inverted_operators(
    const OperatorsProxy &operators_proxy) {
    vector<InvertedOperator> inverted_operators;
    // Exchange preconditions and postconditions.
    for (OperatorProxy op : operators_proxy) {
        vector<FactPair> preconditions = task_properties::get_fact_pairs(op.get_preconditions());
        sort(preconditions.begin(), preconditions.end());
        vector<FactPair> postconditions = get_postconditions(op);
        inverted_operators.emplace_back(move(postconditions), move(preconditions));
    }
    return inverted_operators;
}

InvertedTask::InvertedTask(
    const shared_ptr<AbstractTask> &parent)
    : DelegatingTask(parent),
      operators(compute_inverted_operators(TaskProxy(*parent).get_operators())) {
    if (parent->get_num_axioms() > 0) {
        ABORT("InvertedTask doesn't support axioms.");
    }
    if (task_properties::has_conditional_effects(TaskProxy(*parent))) {
        ABORT("InvertedTask doesn't support conditional effects.");
    }
}

int InvertedTask::get_num_operator_preconditions(int index, bool) const {
    return operators[index].preconditions.size();
}

FactPair InvertedTask::get_operator_precondition(
    int op_index, int fact_index, bool) const {
    return operators[op_index].preconditions[fact_index];
}

int InvertedTask::get_num_operator_effects(int op_index, bool) const {
    return operators[op_index].effects.size();
}

FactPair InvertedTask::get_operator_effect(
    int op_index, int eff_index, bool) const {
    return operators[op_index].effects[eff_index];
}

FactPair InvertedTask::get_goal_fact(int) const {
    ABORT("InvertedTask doesn't support retrieving the goal.");
}

vector<int> InvertedTask::get_initial_state_values() const {
    ABORT("InvertedTask doesn't support retrieving the initial state.");
}
}
