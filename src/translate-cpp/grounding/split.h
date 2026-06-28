#ifndef GROUNDING_SPLIT_H
#define GROUNDING_SPLIT_H

#include "program.h"

namespace translate::grounding {
/*
  Split rules whose conditions fall into disjoint variable-connected
  components, then split each k-ary join (k>=2) into binary joins via
  greedy_join. Each output rule has type JOIN, PRODUCT, or PROJECT.
*/
void split_rules(Program &prog);
}

#endif
