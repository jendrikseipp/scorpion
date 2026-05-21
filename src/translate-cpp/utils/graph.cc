#include "graph.h"

#include <algorithm>
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
}
