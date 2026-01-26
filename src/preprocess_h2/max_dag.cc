#include "max_dag.h"

#include <cassert>
#include <iostream>
#include <map>

using namespace std;

vector<int> MaxDAG::get_result() {
    int num_nodes = weighted_graph.size();
    if (debug) {
        for (int i = 0; i < num_nodes; i++) {
            cout << "From " << i << ":";
            for (const auto &[target, weight] : weighted_graph[i])
                cout << " " << target << " [weight " << weight << "]";
            cout << endl;
        }
    }
    vector<int> incoming_weights; // indexed by the graph's nodes
    incoming_weights.resize(weighted_graph.size(), 0);
    for (const auto &weighted_edges : weighted_graph) {
        for (const auto &[target, weight] : weighted_edges)
            incoming_weights[target] += weight;
    }

    // Build minHeap of nodes, compared by number of incoming edges.
    typedef multimap<int, int>::iterator HeapPosition;

    vector<HeapPosition> heap_positions;
    multimap<int, int> heap;
    for (int node = 0; node < num_nodes; node++) {
        if (debug)
            cout << "node " << node << " has " << incoming_weights[node]
                 << " edges" << endl;
        HeapPosition pos = heap.insert(make_pair(incoming_weights[node], node));
        heap_positions.push_back(pos);
    }
    vector<bool> done;
    done.resize(weighted_graph.size(), false);

    vector<int> result;
    // Recursively delete node with minimal weight of incoming edges.
    while (!heap.empty()) {
        if (debug)
            cout << "minimal element is " << heap.begin()->second << endl;
        int removed = heap.begin()->second;
        done[removed] = true;
        result.push_back(removed);
        heap.erase(heap.begin());
        const vector<pair<int, int>> &succs = weighted_graph[removed];
        for (const auto &[target, arc_weight_raw] : succs) {
            if (!done[target]) {
                int arc_weight = arc_weight_raw;
                while (arc_weight >= 100000)
                    arc_weight -= 100000;
                int new_weight = heap_positions[target]->first - arc_weight;
                heap.erase(heap_positions[target]);
                heap_positions[target] =
                    heap.insert(make_pair(new_weight, target));
                if (debug)
                    cout << "node " << target << " has now " << new_weight
                         << " edges " << endl;
            }
        }
    }
    if (debug) {
        cout << "result: " << endl;
        for (int r : result)
            cout << r << " - ";
        cout << endl;
    }
    return result;
}
