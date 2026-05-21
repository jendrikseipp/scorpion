#include "graph.h"

#include <algorithm>
#include <numeric>
#include <set>

namespace translate::utils {
std::vector<std::pair<std::string, std::string>> transitive_closure(
    const std::vector<std::pair<std::string, std::string>> &pairs) {
    std::set<std::pair<std::string, std::string>> result(pairs.begin(),
                                                         pairs.end());
    std::set<std::string> nodes;
    for (const auto &p : pairs) {
        nodes.insert(p.first);
        nodes.insert(p.second);
    }
    // Warshall over the node set.
    for (const auto &k : nodes) {
        for (const auto &i : nodes) {
            if (result.count({i, k}) == 0) continue;
            for (const auto &j : nodes)
                if (result.count({k, j}))
                    result.insert({i, j});
        }
    }
    return {result.begin(), result.end()};
}

namespace {
int find_root(std::vector<int> &parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}
}

std::vector<int> connected_components(
    std::size_t num_nodes, const std::vector<std::pair<int, int>> &edges) {
    std::vector<int> parent(num_nodes);
    std::iota(parent.begin(), parent.end(), 0);
    for (const auto &[u, v] : edges) {
        int ru = find_root(parent, u);
        int rv = find_root(parent, v);
        if (ru != rv) parent[ru] = rv;
    }
    // Re-number roots to small consecutive ids.
    std::vector<int> id(num_nodes, -1);
    int next_id = 0;
    std::vector<int> result(num_nodes);
    for (std::size_t i = 0; i < num_nodes; ++i) {
        int r = find_root(parent, static_cast<int>(i));
        if (id[r] == -1) id[r] = next_id++;
        result[i] = id[r];
    }
    return result;
}
}
