#include "utils_landmarks.h"

#include "../option_parser.h"

#include "../landmarks/exploration.h"
#include "../landmarks/landmark_factory_h_m.h"
#include "../landmarks/landmark_graph.h"

#include "../utils/memory.h"

#include <algorithm>
#include <fstream>

using namespace std;
using namespace landmarks;

namespace cegar {
static FactPair get_fact(const LandmarkNode &node) {
    /* We assume that the given LandmarkNodes are from an h^m landmark
       graph with m=1. */
    assert(node.facts.size() == 1);
    return node.facts[0];
}

shared_ptr<LandmarkGraph> get_landmark_graph(const shared_ptr<AbstractTask> &task) {
    Options exploration_opts;
    exploration_opts.set<shared_ptr<AbstractTask>>("transform", task);
    exploration_opts.set<bool>("cache_estimates", false);
    Exploration exploration(exploration_opts);

    Options hm_opts;
    hm_opts.set<int>("m", 1);
    // h^m doesn't produce reasonable orders anyway.
    hm_opts.set<bool>("reasonable_orders", false);
    hm_opts.set<bool>("only_causal_landmarks", false);
    hm_opts.set<bool>("disjunctive_landmarks", false);
    hm_opts.set<bool>("conjunctive_landmarks", false);
    hm_opts.set<bool>("no_orders", false);
    hm_opts.set<int>("lm_cost_type", NORMAL);
    LandmarkFactoryHM lm_graph_factory(hm_opts);

    return lm_graph_factory.compute_lm_graph(task, exploration);
}

vector<FactPair> get_fact_landmarks(const LandmarkGraph &graph) {
    vector<FactPair> facts;
    const set<LandmarkNode *> &nodes = graph.get_nodes();
    for (const LandmarkNode *node : nodes) {
        facts.push_back(get_fact(*node));
    }
    sort(facts.begin(), facts.end());
    return facts;
}

VarToValues get_prev_landmarks(const LandmarkGraph &graph, const FactPair &fact) {
    VarToValues groups;
    LandmarkNode *node = graph.get_landmark(fact);
    assert(node);
    vector<const LandmarkNode *> open;
    unordered_set<const LandmarkNode *> closed;
    for (const auto &parent_and_edge : node->parents) {
        const LandmarkNode *parent = parent_and_edge.first;
        open.push_back(parent);
    }
    while (!open.empty()) {
        const LandmarkNode *ancestor = open.back();
        open.pop_back();
        if (closed.find(ancestor) != closed.end())
            continue;
        closed.insert(ancestor);
        FactPair ancestor_fact = get_fact(*ancestor);
        groups[ancestor_fact.var].push_back(ancestor_fact.value);
        for (const auto &parent_and_edge : ancestor->parents) {
            const LandmarkNode *parent = parent_and_edge.first;
            open.push_back(parent);
        }
    }
    return groups;
}

static string get_quoted_node_name(const FactPair &fact) {
    stringstream out;
    out << "\"" << g_fact_names[fact.var][fact.value].substr(5)
        << " (" << fact.var << "=" << fact.value << ")\"";
    return out.str();
}

static bool is_true_in_initial_state(const FactPair &fact) {
    return g_initial_state_data[fact.var] == fact.value;
}

void write_landmark_graph_dot_file(
    const LandmarkGraph &graph, const string &filename) {
    const set<LandmarkNode *> &nodes = graph.get_nodes();

    ofstream dotfile(filename);
    if (!dotfile.is_open()) {
        cerr << "file << " << filename << " could not be opened" << endl;
        utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
    }

    dotfile << "digraph landmarkgraph {" << endl;
    for (const auto *node_p : nodes) {
        const FactPair node_fact = get_fact(*node_p);
        dotfile << get_quoted_node_name(node_fact) << ";" << endl;
        for (const auto &parent_pair : node_p->parents) {
            const LandmarkNode *parent_p = parent_pair.first;
            const FactPair parent_fact = get_fact(*parent_p);
            dotfile << get_quoted_node_name(parent_fact) << " -> "
                    << get_quoted_node_name(node_fact) << ";" << endl;
            // Mark initial state facts green.
            if (is_true_in_initial_state(parent_fact))
                dotfile << get_quoted_node_name(parent_fact)
                        << " [color=green];" << endl;
            if (is_true_in_initial_state(node_fact))
                dotfile << get_quoted_node_name(node_fact)
                        << " [color=green];" << endl;
        }
    }
    // Mark goal facts red if they are false initially, yellow otherwise.
    for (const pair<int, int> &goal_pair : g_goal) {
        FactPair goal(goal_pair.first, goal_pair.second);
        string color = "red";
        if (is_true_in_initial_state(goal))
            color = "yellow";
        dotfile << get_quoted_node_name(goal)
                << " [color=" << color << "];" << endl;
    }
    dotfile << "}" << endl;
    dotfile.close();
}
}
