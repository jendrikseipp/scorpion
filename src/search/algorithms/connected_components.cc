#include "connected_components.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <utility>

using namespace std;

namespace ccp {
static void dfs(
    const vector<vector<int>> &graph, int vertex, vector<bool> &visited,
    vector<int> &component) {
    visited[vertex] = true;
    component.push_back(vertex);
    for (int successor : graph[vertex]) {
        if (!visited[successor]) {
            dfs(graph, successor, visited, component);
        }
    }
}

vector<vector<int>> compute_connected_components(
    const vector<vector<int>> &graph) {
    size_t node_count = graph.size();
    vector<bool> visited(graph.size(), false);
    vector<vector<int>> components;
    components.reserve(node_count / 4);
    for (size_t i = 0; i < node_count; ++i) {
        if (!visited[i]) {
            vector<int> component;
            component.reserve(node_count / 4);
            dfs(graph, i, visited, component);
            components.push_back(component);
        }
    }
    return components;
}

DisjointSet::DisjointSet(int n) : parent(n), sz(n, 1) {
    iota(parent.begin(), parent.end(), 0);
}

int DisjointSet::find(int i) {
    int root = i;
    while (parent[root] != root) {
        root = parent[root];
    }
    // Path compression: make all elements on the path point to the root.
    while (parent[i] != root) {
        int p = parent[i];
        parent[i] = root;
        i = p;
    }
    return root;
}

bool DisjointSet::unite(int i, int j) {
    int root_i = find(i);
    int root_j = find(j);
    if (root_i == root_j) {
        return false;
    }
    // Union by size: attach the smaller tree to the root of the larger tree.
    if (sz[root_i] < sz[root_j]) {
        swap(root_i, root_j);
    }
    parent[root_j] = root_i;
    sz[root_i] += sz[root_j];
    return true;
}

int DisjointSet::get_num_sets() const {
    int count = 0;
    for (size_t i = 0; i < parent.size(); ++i) {
        // If an element is its own parent, it is a root.
        if (parent[i] == static_cast<int>(i)) {
            ++count;
        }
    }
    return count;
}

vector<vector<int>> DisjointSet::get_connected_components() {
    // Group elements by their representative.
    unordered_map<int, vector<int>> components_map;
    for (size_t i = 0; i < parent.size(); ++i) {
        components_map[find(i)].push_back(i);
    }
    vector<vector<int>> components;
    components.reserve(components_map.size());
    for (auto &[root, elements] : components_map) {
        components.push_back(move(elements));
    }
    return components;
}

vector<vector<int>> DisjointSet::get_connected_components(
    const vector<int> &elements) {
    // Fast path for the common case that all elements are in one set.
    if (!elements.empty()) {
        int first_root = find(elements[0]);
        if (all_of(elements.begin(), elements.end(),
                   [&](int i) {return find(i) == first_root;})) {
            return {elements};
        }
    }
    // Group elements by their representative.
    unordered_map<int, vector<int>> components_map;
    for (int i : elements) {
        components_map[find(i)].push_back(i);
    }
    vector<vector<int>> components;
    components.reserve(components_map.size());
    for (auto &[root, component] : components_map) {
        components.push_back(move(component));
    }
    return components;
}

size_t DisjointSet::get_set_size(int i) {
    return sz[find(i)];
}
}
