#ifndef NORMALIZE_NORMALIZE_H
#define NORMALIZE_NORMALIZE_H

#include "../pddl/task.h"

namespace translate::normalize {
/*
  Normalize the parsed PDDL task in-place:
    [1] remove universal quantifiers (replace by negated derived predicates),
    [2] substitute non-trivial goal by a derived predicate,
    [3] pull disjunctions to the root,
    [4] split conditions at the outermost disjunction (duplicating owners),
    [5] move existential quantifiers out of conjunctions,
    [6] eliminate existential quantifiers from axioms / preconditions /
        effect conditions by lifting their parameters into the surrounding
        owner.
    [7] verify that derived predicates are not used in init or action
        effects.
  Mirrors translate/normalize.py.
*/
void normalize(pddl::Task &task);
}

#endif
