#include "inverted_task.h"

#include "../utils/system.h"

using namespace std;

namespace extra_tasks {
/*
  If we need the same functionality again in another task, we can move this to
  actract_task.h. We should then document that this method is only supposed to
  be used from within AbstractTasks. More high-level users should use
  has_conditional_effects(TaskProxy) from task_tools.h instead.
*/
static bool has_conditional_effects(const AbstractTask &task) {
    int num_ops = task.get_num_operators();
    for (int op_index = 0; op_index < num_ops; ++op_index) {
        int num_effs = task.get_num_operator_effects(op_index, false);
        for (int eff_index = 0; eff_index < num_effs; ++eff_index) {
            int num_conditions = task.get_num_operator_effect_conditions(
                op_index, eff_index, false);
            if (num_conditions > 0) {
                return true;
            }
        }
    }
    return false;
}

InvertedTask::InvertedTask(
    const shared_ptr<AbstractTask> &parent,
    vector<InvertedOperator> &&inverted_operators)
    : DelegatingTask(parent),
      operators(move(inverted_operators)){
    if (parent->get_num_axioms() > 0) {
        ABORT("InvertedTask doesn't support axioms.");
    }
    if (has_conditional_effects(*parent)) {
        ABORT("InvertedTask doesn't support conditional effects.");
    }
}

int InvertedTask::get_operator_cost(int index, bool ) const {
    return operators[index].cost;
}

string InvertedTask::get_operator_name(int, bool) const {
    ABORT("InvertedTask doesn't support retrieving operator names.");
}

int InvertedTask::get_num_operators() const {
    return operators.size();
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

int InvertedTask::get_num_operator_effect_conditions(
    int, int, bool) const {
    return 0;
}

FactPair InvertedTask::get_operator_effect_condition(
    int, int, int, bool) const {
    ABORT("InvertedTask doesn't support conditional effects.");
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
