#include "sccs.h"

#include <algorithm>
#include <vector>

using namespace std;
namespace translate::utils {
namespace {
struct TarjanState {
    const vector<vector<int>> &graph;
    vector<int> index;
    vector<int> lowlink;
    vector<int> stack_idx;
    vector<int> stack;
    vector<vector<int>> sccs;
    int current_index = 1;

    explicit TarjanState(const vector<vector<int>> &g)
        : graph(g), index(g.size(), 0), lowlink(g.size(), 0),
          stack_idx(g.size(), -1) {}

    void visit(int v) {
        // Iterative DFS using an explicit stack frame.
        struct Frame {
            int v;
            int next_succ;
        };
        vector<Frame> work;
        work.push_back({v, 0});
        index[v] = current_index;
        lowlink[v] = current_index;
        ++current_index;
        stack_idx[v] = static_cast<int>(stack.size());
        stack.push_back(v);

        while (!work.empty()) {
            Frame &f = work.back();
            const auto &succs = graph[f.v];
            if (f.next_succ < static_cast<int>(succs.size())) {
                int w = succs[f.next_succ++];
                if (index[w] == 0) {
                    index[w] = current_index;
                    lowlink[w] = current_index;
                    ++current_index;
                    stack_idx[w] = static_cast<int>(stack.size());
                    stack.push_back(w);
                    work.push_back({w, 0});
                } else if (stack_idx[w] >= 0) {
                    lowlink[f.v] = min(lowlink[f.v], index[w]);
                }
            } else {
                int cur = f.v;
                work.pop_back();
                if (lowlink[cur] == index[cur]) {
                    /*
                      Python's sccs.py extracts the SCC as
                      `stack[stack_index:]`, giving the nodes in
                      forward DFS visit order. Mirror that here so
                      MaxDAG's input_order tie-breaking matches Python
                      byte-for-byte; previously we pushed back-to-front
                      and produced reverse order, which led to
                      different variable numbering downstream.
                    */
                    size_t scc_begin = stack.size();
                    int top;
                    do {
                        --scc_begin;
                        top = stack[scc_begin];
                    } while (top != cur);
                    vector<int> scc(stack.begin() + scc_begin,
                                         stack.end());
                    for (int n : scc) stack_idx[n] = -1;
                    stack.resize(scc_begin);
                    sccs.push_back(move(scc));
                }
                if (!work.empty())
                    lowlink[work.back().v] =
                        min(lowlink[work.back().v], lowlink[cur]);
            }
        }
    }
};
}

vector<vector<int>> get_sccs_adjacency_list(
    const vector<vector<int>> &adjacency_list) {
    TarjanState ts(adjacency_list);
    for (size_t i = 0; i < adjacency_list.size(); ++i)
        if (ts.index[i] == 0) ts.visit(static_cast<int>(i));
    reverse(ts.sccs.begin(), ts.sccs.end());
    return ts.sccs;
}
}
