#ifndef UTILS_GRAPH_H
#define UTILS_GRAPH_H

#include <cstddef>
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

/*
  Connected components of an undirected graph over node indices [0, n).
  The `edges` are unordered pairs (u, v). Returns the component
  assignment vector: result[i] is the component id (a small int) of
  node i.
*/
std::vector<int> connected_components(
    std::size_t num_nodes,
    const std::vector<std::pair<int, int>> &edges);
}

#endif
