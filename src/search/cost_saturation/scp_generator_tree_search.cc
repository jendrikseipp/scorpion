#include "scp_generator_tree_search.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../utils/logging.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace cost_saturation {
SCPGeneratorTreeSearch::SCPGeneratorTreeSearch(const Options &opts)
    : SCPGenerator(opts) {}

void SCPGeneratorTreeSearch::initialize(
    const TaskProxy &task_proxy,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<StateMap> & /*state_maps*/,
    const vector<int> &costs) {
    num_abstractions = abstractions.size();

    int num_operators = task_proxy.get_operators().size();
    vector<vector<bool>> dependent_ops;
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        vector<bool> active_ops_bitset(num_operators, false);
        for (int op_id : abstraction->get_active_operators()) {
            active_ops_bitset[op_id] = true;
        }
        dependent_ops.push_back(move(active_ops_bitset));
    }

    // Remove all zero cost operators from set of dependent operators.
    // TODO: This will be more important during the order creation
    // process as computing SCPs will leave us with more zero cost
    // operators.
    for (size_t i = 0; i < costs.size(); ++i) {
        if (costs[i] == 0) {
            for (vector<bool> &dep_ops : dependent_ops) {
                dep_ops[i] = false;
            }
        }
    }

    assert(dependent_ops.size() == num_abstractions);

    for (size_t i = 0; i < num_abstractions; ++i) {
        vertices.insert(i);
    }

    int num_pairs = 0;
    int num_independent_pairs = 0;

    edges.resize(num_abstractions, vector<int>());
    for (size_t i = 0; i < dependent_ops.size(); ++i) {
        for (size_t j = i + 1; j < dependent_ops.size(); ++j) {
            if (disjunct(dependent_ops[i], dependent_ops[j])) {
                ++num_independent_pairs;
            } else {
                edges[i].push_back(j);
                edges[j].push_back(i);
            }
            ++num_pairs;
        }
    }

    root_node = utils::make_unique_ptr<SearchNode>();

    cout << num_independent_pairs << "/" << num_pairs << " = "
         << (num_pairs ? num_independent_pairs * 100. / num_pairs : 100.)
         << "% of abstraction pairs are independent" << endl;
}

bool SCPGeneratorTreeSearch::has_next_cost_partitioning() const {
    return !root_node->solved;
}

CostPartitioning SCPGeneratorTreeSearch::get_next_cost_partitioning(
    const TaskProxy & /*task_proxy*/,
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<StateMap> & /*state_maps*/,
    const vector<int> &costs) {
    current_order.clear();
    current_vertices = vertices;
    current_edges = edges;
    visit_node(*root_node);
    return compute_saturated_cost_partitioning(abstractions, current_order, costs);
}

void SCPGeneratorTreeSearch::visit_node(SearchNode &node) {
    // cout << "visit node with order: " << node.order << endl;

    if (current_order.size() == num_abstractions) {
        node.solved = true;
        return;
    }

    if (node.children.empty()) {
        // Initialize children as null pointers
        for (size_t i = 0; i < current_vertices.size(); ++i) {
            node.children.push_back(nullptr);
        }
    }

    vector<pair<int, int>> candidates;
    int min_visits = 100000000;

    // cout << "num children: " << node.children.size() << endl;
    int index = 0;
    for (int vertex : current_vertices) {
        //for (size_t i = 0; i < node.children.size(); ++i) {
        unique_ptr<SearchNode> &child = node.children[index];
        if (!child) {
            // cout << "child " << i << " is not explicated yet." << endl;
            if (min_visits > 0) {
                min_visits = 0;
                candidates.clear();
            }
            candidates.push_back(make_pair(index, vertex));
        } else if (!child->solved) {
            // cout << "child " << i << " is explicated and has " << child->num_visits << " visits." << endl;
            if (child->num_visits < min_visits) {
                min_visits = child->num_visits;
                candidates.clear();
                candidates.push_back(make_pair(index, vertex));
            } else if (child->num_visits == min_visits) {
                candidates.push_back(make_pair(index, vertex));
            }
        }
        ++index;
    }
    assert(!candidates.empty());

    pair<int, int> succ = candidates[rand() % candidates.size()];
    assert(succ.first < (int)node.children.size());
    if (!node.children[succ.first]) {
        node.children[succ.first] = utils::make_unique_ptr<SearchNode>();
    }

    remove_vertex(succ.second);

    // continue rollout
    visit_node(*node.children[succ.first]);

    ++node.num_visits;
    node.solved = true;
    for (size_t i = 0; i < node.children.size(); ++i) {
        if (!node.children[i] || !node.children[i]->solved) {
            node.solved = false;
            break;
        }
    }
}

void SCPGeneratorTreeSearch::remove_vertex(int vertex) {
    assert(current_vertices.find(vertex) != current_vertices.end());
    current_vertices.erase(vertex);
    current_order.push_back(vertex);

    for (int v : current_edges[vertex]) {
        assert(std::find(current_edges[v].begin(), current_edges[v].end(), vertex) != current_edges[v].end());
        current_edges[v].erase(std::find(current_edges[v].begin(), current_edges[v].end(), vertex));

        if (current_edges[v].empty()) {
            current_vertices.erase(v);
            current_order.push_back(v);
        }
    }
    current_edges[vertex].clear();
}

bool SCPGeneratorTreeSearch::disjunct(vector<bool> &v1, vector<bool> &v2) const {
    assert(v1.size() == v2.size());
    for (size_t i = 0; i < v1.size(); ++i) {
        if (v1[i] && v2[i]) {
            return false;
        }
    }
    return true;
}

static shared_ptr<SCPGenerator> _parse_tree_search(OptionParser &parser) {
    add_common_scp_generator_options_to_parser(parser);
    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<SCPGeneratorTreeSearch>(opts);
}

static PluginShared<SCPGenerator> _plugin_tree_search(
    "tree_search", _parse_tree_search);
}
