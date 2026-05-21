#ifndef TRANSLATE_SIMPLIFY_SIMPLIFY_H
#define TRANSLATE_SIMPLIFY_SIMPLIFY_H

#include "../sas/sas_task.h"

#include <exception>

namespace translate::simplify {
class Impossible : public std::exception {};
class TriviallySolvable : public std::exception {};

/*
  Remove unreachable propositions and prune variables with only one value.
  Mutates `task` in place. Throws Impossible if the task is detected to
  be unsolvable, or TriviallySolvable if the goal becomes empty.
*/
void filter_unreachable_propositions(sas::SASTask &task);
}

#endif
