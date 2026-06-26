#ifndef GROUNDING_BUILD_H
#define GROUNDING_BUILD_H

#include "program.h"

#include "../pddl/task.h"

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
