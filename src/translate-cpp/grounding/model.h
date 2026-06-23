#ifndef GROUNDING_MODEL_H
#define GROUNDING_MODEL_H

#include "program.h"

#include <vector>

namespace translate::grounding {
/*
  Run semi-naive evaluation of the Datalog program. Returns the full
  list of derived ground atoms in the order they were derived.

  The program must have been split (split_rules()) so that each rule has
  a kind of JOIN, PRODUCT, or PROJECT.
*/
std::vector<Atom> compute_model(const Program &prog);
}

#endif
