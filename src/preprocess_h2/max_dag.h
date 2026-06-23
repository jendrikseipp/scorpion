#ifndef MAX_DAG_H
#define MAX_DAG_H

#include <vector>

class MaxDAG {
    const std::vector<std::vector<std::pair<int, int>>> &weighted_graph;
    bool debug;
public:
    explicit MaxDAG(const std::vector<std::vector<std::pair<int, int>>> &graph)
        : weighted_graph(graph), debug(false) {
    }
    std::vector<int> get_result();
};
#endif
