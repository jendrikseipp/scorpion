#include "graph.h"

#include <algorithm>
#include <numeric>
#include <set>

using namespace std;
namespace translate::utils {
vector<pair<string, string>> transitive_closure(
    const vector<pair<string, string>> &pairs) {
    set<pair<string, string>> result(pairs.begin(),
                                                         pairs.end());
    set<string> nodes;
    for (const auto &p : pairs) {
        nodes.insert(p.first);
        nodes.insert(p.second);
    }
    // Warshall over the node set.
    for (const auto &k : nodes) {
        for (const auto &i : nodes) {
            if (!result.contains({i, k})) continue;
            for (const auto &j : nodes)
                if (result.contains({k, j}))
                    result.insert({i, j});
        }
    }
    return {result.begin(), result.end()};
}

namespace {
int find_root(vector<int> &parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}
}

vector<int> connected_components(
    size_t num_nodes, const vector<pair<int, int>> &edges) {
    vector<int> parent(num_nodes);
    iota(parent.begin(), parent.end(), 0);
    for (const auto &[u, v] : edges) {
        int ru = find_root(parent, u);
        int rv = find_root(parent, v);
        if (ru != rv) parent[ru] = rv;
    }
    // Re-number roots to small consecutive ids.
    vector<int> id(num_nodes, -1);
    int next_id = 0;
    vector<int> result(num_nodes);
    for (size_t i = 0; i < num_nodes; ++i) {
        int r = find_root(parent, static_cast<int>(i));
        if (id[r] == -1) id[r] = next_id++;
        result[i] = id[r];
    }
    return result;
}
}
