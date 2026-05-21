#ifndef TRANSLATE_UTILS_SCCS_H
#define TRANSLATE_UTILS_SCCS_H

#include <vector>

namespace translate::utils {
/*
  Tarjan's algorithm for maximal strongly connected components.
  `adjacency_list[u]` is the list of successors of node u.
  Returns the SCCs in topological-sort order (each SCC is a vector of
  node indices).
*/
std::vector<std::vector<int>> get_sccs_adjacency_list(
    const std::vector<std::vector<int>> &adjacency_list);
}

#endif
