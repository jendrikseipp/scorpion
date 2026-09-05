#ifndef NORMALIZE_NORMALIZE_H
#define NORMALIZE_NORMALIZE_H

#include "../pddl/task.h"

namespace translate::normalize {
/*
  Normalize the parsed PDDL task in-place:
    [1] eliminate universal quantifiers (replace by negated derived
        predicates),
    [2] simplify conditions according to
        --condition-normalization-strategy, leaving only conjunctions and
        existential conditions (plus truth values and literals):
          dnf: substitute a non-trivial goal by a derived predicate and pull
            disjunctions to the root,
          axiomatize_disjunctions: substitute a non-trivial goal by a derived
            predicate and replace every disjunction by a derived predicate,
          axiomatize_disjunctions_existentials: additionally replace
            existential quantifiers in action conditions and the goal by
            derived predicates;
        then split conditions at the outermost disjunction (duplicating
        owners),
    [3] eliminate existential quantifiers: move them out of conjunctions and
        then lift them from axioms / preconditions / effect conditions into
        the surrounding owner's parameters,
    and verify that derived predicates are not used in init or action effects.
  Mirrors fast_downward/translate/normalize.py.
*/
void normalize(pddl::Task &task);
}

#endif
