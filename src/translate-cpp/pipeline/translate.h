#ifndef TRANSLATE_PIPELINE_TRANSLATE_H
#define TRANSLATE_PIPELINE_TRANSLATE_H

#include "../pddl/task.h"
#include "../sas/sas_task.h"

namespace translate::pipeline {
/*
  Run the full translation pipeline from a (parsed, normalized) PDDL Task
  to a SAS+ task. Mirrors translate/main.pddl_to_sas.
*/
sas::SASTask pddl_to_sas(pddl::Task &task);
}

#endif
