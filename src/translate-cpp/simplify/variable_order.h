#ifndef SIMPLIFY_VARIABLE_ORDER_H
#define SIMPLIFY_VARIABLE_ORDER_H

#include "../sas/sas_task.h"

namespace translate::simplify {
/*
  Reorder SAS variables based on the (weighted) causal graph and
  optionally remove variables that are not relevant to the goal.

  Mirrors translate/variable_order.py.find_and_apply_variable_order.
*/
void find_and_apply_variable_order(sas::SASTask &task, bool reorder_vars,
                                   bool filter_unimportant_vars);
}

#endif
