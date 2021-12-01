#ifndef PDBS_TYPES_H
#define PDBS_TYPES_H

#include "../utils/hash.h"

#include <memory>
#include <vector>

namespace partial_state_tree {
class PartialStateTree;
}

namespace cost_saturation {
class Projection;
}

namespace pdbs {
using DeadEnds = partial_state_tree::PartialStateTree;
class PatternDatabase;
using Pattern = std::vector<int>;
using PatternCollection = std::vector<Pattern>;
using PDBCollection = std::vector<std::shared_ptr<PatternDatabase>>;
using ProjectionCollection = std::vector<std::unique_ptr<cost_saturation::Projection>>;
using PatternSet = utils::HashSet<Pattern>;
using PatternID = int;
/* NOTE: pattern cliques are often called maximal additive pattern subsets
   in the literature. A pattern clique is an additive set of patterns,
   represented by their IDs (indices) in a pattern collection. */
using PatternClique = std::vector<PatternID>;
}

#endif
