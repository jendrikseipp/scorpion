#ifndef ALGORITHMS_CONNECTED_COMPONENTS_H
#define ALGORITHMS_CONNECTED_COMPONENTS_H

#include <cstddef>
#include <vector>

namespace ccp {
std::vector<std::vector<int>> compute_connected_components(
    const std::vector<std::vector<int>> &graph);

class DisjointSet {
    std::vector<int> parent;
    // sz[i] stores the size of the set if i is the root.
    std::vector<std::size_t> sz;

public:
    // Initialize n disjoint sets, one for each element from 0 to n-1.
    explicit DisjointSet(int n);

    // Return the representative (root) of the set containing element i,
    // using path compression.
    int find(int i);

    /* Merge the sets containing elements i and j. Return true if a union
       occurred (i.e., i and j were in different sets), false otherwise. */
    bool unite(int i, int j);

    int get_num_sets() const;

    // Get all connected components (partitions).
    std::vector<std::vector<int>> get_connected_components();

    // Get the connected components of the given elements.
    std::vector<std::vector<int>> get_connected_components(
        const std::vector<int> &elements);

    // Get the size of the set containing a specific element.
    std::size_t get_set_size(int i);
};
}

#endif
