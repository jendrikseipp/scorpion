#ifndef TRANSLATE_UTILS_GRAPH_H
#define TRANSLATE_UTILS_GRAPH_H

#include <string>
#include <utility>
#include <vector>

namespace translate::utils {
/*
  Transitive closure of a binary relation expressed as a list of
  (descendant, ancestor) pairs. Returns the closure as a sorted vector of
  pairs. Used by set_supertypes() to compute supertype names.
*/
std::vector<std::pair<std::string, std::string>> transitive_closure(
    const std::vector<std::pair<std::string, std::string>> &pairs);
}

#endif
