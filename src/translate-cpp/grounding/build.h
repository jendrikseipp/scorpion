#ifndef TRANSLATE_GROUNDING_BUILD_H
#define TRANSLATE_GROUNDING_BUILD_H

#include "../pddl/task.h"
#include "program.h"

namespace translate::grounding {
/*
  Build the Datalog Program that represents reachability for the given
  task. The task must already be normalized (see normalize::normalize).
  The returned Program is also normalized (Program::normalize() has been
  called) but not yet split (split_rules() applies later).
*/
Program build_program(const pddl::Task &task);
}

#endif
