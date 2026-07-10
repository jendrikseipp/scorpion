#include "variable_order.h"

#include "../translate_options.h"

#include "../utils/sccs.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
namespace translate::simplify {
using namespace sas;

namespace {
// Weight added to each causal-graph edge pointing at a goal variable, so goal
// variables accumulate a large incoming weight and MaxDAG orders them last. The
// boost is far larger than any real edge weight, so `w % GOAL_EDGE_WEIGHT`
// recovers the unboosted weight. Mirrors src/translate/variable_order.py.
constexpr int GOAL_EDGE_WEIGHT = 100000;

class CausalGraph {
public:
    vector<vector<pair<int, int>>>
        weighted_graph; // src -> sorted (tgt, weight)
    // tgt -> its predecessors (deduplicated). Derived once from weighted_graph
    // after weighting rather than maintained per edge: weighted_graph[src]
    // already holds each src->tgt edge exactly once, so one pass yields the
    // dedup'd predecessor lists without a set<int> insert (plus a tree-node
    // malloc) on every one of the millions of operator-effect-source edges.
    vector<vector<int>> predecessor_graph;
    int num_variables;
    unordered_map<int, int> goal_map;

    explicit CausalGraph(const SASTask &task) {
        num_variables = static_cast<int>(task.variables.ranges.size());
        weighted_graph.assign(num_variables, {});
        for (const auto &[v, val] : task.goal.pairs)
            goal_map[v] = val;
        // Collect every src->tgt edge occurrence flat (one push per occurrence,
        // no per-edge tree node), then sort+reduce each source's targets into
        // (tgt, weight) pairs. This replaces `++weighted_graph[src][tgt]` on a
        // std::map over millions of operator-effect edges with a contiguous
        // sort; weighted_graph stays sorted by tgt, as get_ordering requires.
        vector<vector<int>> raw_targets(num_variables);
        weight_from_ops(task.operators, raw_targets);
        weight_from_axioms(task.axioms, raw_targets);
        for (int src = 0; src < num_variables; ++src)
            weighted_graph[src] = reduce_to_weighted(move(raw_targets[src]));
        build_predecessor_graph();
    }

    // Sort target occurrences and run-length-reduce them to (tgt, weight).
    static vector<pair<int, int>> reduce_to_weighted(vector<int> targets) {
        ranges::sort(targets);
        vector<pair<int, int>> weighted;
        for (int tgt : targets) {
            if (!weighted.empty() && weighted.back().first == tgt)
                ++weighted.back().second;
            else
                weighted.emplace_back(tgt, 1);
        }
        return weighted;
    }

    void build_predecessor_graph() {
        predecessor_graph.assign(num_variables, {});
        for (int src = 0; src < num_variables; ++src)
            for (const auto &[tgt, _] : weighted_graph[src])
                predecessor_graph[tgt].push_back(src);
    }

    void weight_from_ops(
        const vector<SASOperator> &operators,
        vector<vector<int>> &raw_targets) {
        for (const auto &op : operators) {
            vector<int> source_vars;
            source_vars.reserve(op.prevail.size() + op.pre_post.size());
            for (const auto &[v, _] : op.prevail)
                source_vars.push_back(v);
            for (const auto &[v, pre, post, cond] : op.pre_post)
                if (pre != -1)
                    source_vars.push_back(v);
            for (const auto &[tgt, pre, post, cond] : op.pre_post) {
                // Sources for this effect are the operator's source_vars plus
                // this effect's own condition variables. Iterate both in place
                // rather than copy source_vars and append per effect (the
                // condition is empty on STRIPS, so the copy bought nothing).
                auto add_edge = [&](int src) {
                    if (src != tgt)
                        raw_targets[src].push_back(tgt);
                };
                for (int src : source_vars)
                    add_edge(src);
                for (const auto &[cv, cval] : cond)
                    add_edge(cv);
            }
        }
    }

    void weight_from_axioms(
        const vector<SASAxiom> &axioms, vector<vector<int>> &raw_targets) {
        for (const auto &ax : axioms) {
            int tgt = ax.effect.first;
            for (const auto &[src, _] : ax.condition) {
                if (src != tgt)
                    raw_targets[src].push_back(tgt);
            }
        }
    }

    vector<vector<int>> get_sccs() const {
        vector<vector<int>> adj(num_variables);
        for (int s = 0; s < num_variables; ++s)
            for (const auto &[t, _] : weighted_graph[s])
                adj[s].push_back(t);
        for (auto &v : adj)
            ranges::sort(v);
        return utils::get_sccs_adjacency_list(adj);
    }

    vector<int> get_ordering() const {
        auto sccs = get_sccs();
        vector<int> order;
        for (const auto &scc : sccs) {
            if (scc.size() == 1) {
                order.push_back(scc.front());
                continue;
            }
            // Build the subgraph induced by `scc`. For each edge into a
            // goal var, emit *two* edges: one tagged with +100000 (so
            // the goal node accumulates a large "incoming" weight and
            // is therefore picked last by MaxDAG), plus the unboosted
            // edge for the actual decrement bookkeeping. Mirrors the
            // construction in src/translate/variable_order.py.
            unordered_set<int> scc_set(scc.begin(), scc.end());
            unordered_map<int, vector<pair<int, int>>> subgraph;
            for (int var : scc) {
                auto &edges = subgraph[var];
                // weighted_graph[var] is already sorted by target id
                // (see reduce_to_weighted), matching Python's
                // sorted(items()).
                for (const auto &[tgt, cost] : weighted_graph[var]) {
                    if (!scc_set.contains(tgt))
                        continue;
                    if (goal_map.contains(tgt))
                        edges.emplace_back(tgt, GOAL_EDGE_WEIGHT + cost);
                    edges.emplace_back(tgt, cost);
                }
            }
            auto sub_order = max_dag_order(subgraph, scc);
            order.insert(order.end(), sub_order.begin(), sub_order.end());
        }
        return order;
    }

    /*
      Greedy variable ordering for one SCC of the (weighted) causal
      graph -- the C++ port of MaxDAG.get_result() in
      src/translate/variable_order.py.

      We pick repeatedly the node with the smallest cumulated weight of
      *remaining* incoming edges, breaking ties by the input order
      (which the caller passes as the SCC's original order). Goal vars
      get a +100000 boost per incoming edge so they're picked last,
      matching the Python tie-breaking.

      The data structures mirror Python's heapq + defaultdict(deque)
      with lazy deletion. We need exactly the same outcome as Python
      because LAMA-first's landmark / FF heuristic is extremely
      sensitive to the variable ordering chosen here -- a different
      (but legal) order can cause search to explore 20-70x more states
      on parking-sat14-strips.
    */
    static vector<int> max_dag_order(
        const unordered_map<int, vector<pair<int, int>>> &subgraph,
        const vector<int> &input_order) {
        unordered_map<int, int> incoming_weights;
        for (const auto &[_src, edges] : subgraph) {
            for (const auto &[tgt, w] : edges)
                incoming_weights[tgt] += w;
        }

        // weight -> nodes with that current incoming weight, FIFO in
        // input order.
        unordered_map<int, deque<int>> weight_to_nodes;
        for (int node : input_order) {
            int w = incoming_weights[node]; // 0 if not seen
            weight_to_nodes[w].push_back(node);
        }

        // Min-heap of distinct weight values (lazy deletion: we never
        // remove eagerly, only the bucket-empty case).
        priority_queue<int, vector<int>, greater<int>> weights;
        {
            unordered_set<int> seen;
            for (const auto &[w, _] : weight_to_nodes)
                if (seen.insert(w).second)
                    weights.push(w);
        }

        unordered_set<int> done;
        vector<int> result;
        result.reserve(input_order.size());
        while (!weights.empty()) {
            int min_key = weights.top();
            // `weight_to_nodes[min_key]` may create an empty deque if
            // min_key was previously erased -- that's intentional and
            // matches Python's defaultdict.
            auto &entries = weight_to_nodes[min_key];
            int min_elem = -1;
            bool elem_found = false;
            // A popped node is not a valid pick if none was found yet, it is
            // already placed, or its live incoming weight has since dropped
            // below the bucket key it was filed under (it was re-filed
            // cheaper).
            auto invalid_pick = [&] {
                return !elem_found || done.contains(min_elem) ||
                       min_key > incoming_weights[min_elem];
            };
            while (!entries.empty() && invalid_pick()) {
                min_elem = entries.front();
                entries.pop_front();
                elem_found = true;
            }
            if (entries.empty()) {
                weight_to_nodes.erase(min_key);
                weights.pop();
            }
            if (invalid_pick())
                continue;

            done.insert(min_elem);
            result.push_back(min_elem);
            auto sit = subgraph.find(min_elem);
            if (sit == subgraph.end())
                continue;
            for (const auto &[target, w] : sit->second) {
                if (done.contains(target))
                    continue;
                int decrement = w % GOAL_EDGE_WEIGHT;
                if (decrement == 0)
                    continue;
                int old_iw = incoming_weights[target];
                int new_iw = old_iw - decrement;
                incoming_weights[target] = new_iw;
                // Lazy heap entry: only push the weight if no bucket
                // yet exists for it.
                if (!weight_to_nodes.contains(new_iw))
                    weights.push(new_iw);
                weight_to_nodes[new_iw].push_back(target);
            }
        }
        return result;
    }

    unordered_set<int> important_vars(const SASGoal &goal) const {
        unordered_set<int> necessary;
        vector<int> stack;
        for (const auto &[v, _] : goal.pairs) {
            if (necessary.insert(v).second)
                stack.push_back(v);
        }
        while (!stack.empty()) {
            int n = stack.back();
            stack.pop_back();
            for (int pred : predecessor_graph[n])
                if (necessary.insert(pred).second)
                    stack.push_back(pred);
        }
        return necessary;
    }
};

class VariableOrder {
public:
    vector<int> ordering;
    unordered_map<int, int> new_var;

    explicit VariableOrder(vector<int> ord) : ordering(move(ord)) {
        for (int i = 0; i < static_cast<int>(ordering.size()); ++i)
            new_var[ordering[i]] = i;
    }

    void apply(SASTask &task) const {
        // Variables.
        vector<int> ranges, layers;
        vector<vector<string>> names;
        for (int var : ordering) {
            ranges.push_back(task.variables.ranges[var]);
            layers.push_back(task.variables.axiom_layers[var]);
            names.push_back(task.variables.value_names[var]);
        }
        task.variables.ranges = move(ranges);
        task.variables.axiom_layers = move(layers);
        task.variables.value_names = move(names);
        // Init.
        vector<int> new_init;
        new_init.reserve(ordering.size());
        for (int var : ordering)
            new_init.push_back(task.init.values[var]);
        task.init.values = move(new_init);
        // Goal.
        vector<VarVal> new_goal;
        for (const auto &[v, val] : task.goal.pairs) {
            auto it = new_var.find(v);
            if (it != new_var.end())
                new_goal.emplace_back(it->second, val);
        }
        ranges::sort(new_goal);
        task.goal.pairs = move(new_goal);
        // Mutexes.
        vector<SASMutexGroup> new_mutexes;
        for (auto &m : task.mutexes) {
            vector<VarVal> facts;
            set<int> vars;
            for (const auto &[v, val] : m.facts) {
                auto it = new_var.find(v);
                if (it != new_var.end()) {
                    facts.emplace_back(it->second, val);
                    vars.insert(it->second);
                }
            }
            if (vars.size() > 1) {
                m.facts = move(facts);
                new_mutexes.push_back(move(m));
            }
        }
        cout << new_mutexes.size() << " of " << task.mutexes.size()
             << " mutex groups necessary." << endl;
        task.mutexes = move(new_mutexes);
        // Operators.
        vector<SASOperator> new_ops;
        for (auto &op : task.operators) {
            vector<PrePost> new_pre_post;
            for (auto &[v, pre, post, cond] : op.pre_post) {
                auto it = new_var.find(v);
                if (it == new_var.end())
                    continue;
                vector<VarVal> new_cond;
                for (const auto &[cv, cval] : cond) {
                    auto cit = new_var.find(cv);
                    if (cit != new_var.end())
                        new_cond.emplace_back(cit->second, cval);
                }
                new_pre_post.emplace_back(
                    it->second, pre, post, move(new_cond));
            }
            if (new_pre_post.empty() && !get_options().keep_no_ops)
                continue;
            vector<VarVal> new_prevail;
            for (const auto &[v, val] : op.prevail) {
                auto it = new_var.find(v);
                if (it != new_var.end())
                    new_prevail.emplace_back(it->second, val);
            }
            op.prevail = move(new_prevail);
            op.pre_post = move(new_pre_post);
            new_ops.push_back(move(op));
        }
        cout << new_ops.size() << " of " << task.operators.size()
             << " operators necessary." << endl;
        task.operators = move(new_ops);
        // Axioms.
        vector<SASAxiom> new_ax;
        for (auto &ax : task.axioms) {
            auto it = new_var.find(ax.effect.first);
            if (it == new_var.end())
                continue;
            vector<VarVal> new_cond;
            for (const auto &[v, val] : ax.condition) {
                auto cit = new_var.find(v);
                if (cit != new_var.end())
                    new_cond.emplace_back(cit->second, val);
            }
            ax.condition = move(new_cond);
            ax.effect = {it->second, ax.effect.second};
            new_ax.push_back(move(ax));
        }
        cout << new_ax.size() << " of " << task.axioms.size()
             << " axiom rules necessary." << endl;
        task.axioms = move(new_ax);
    }
};
}

void find_and_apply_variable_order(
    SASTask &task, bool reorder_vars, bool filter_unimportant_vars) {
    if (!reorder_vars && !filter_unimportant_vars)
        return;
    CausalGraph cg(task);
    vector<int> order;
    if (reorder_vars) {
        order = cg.get_ordering();
    } else {
        order.resize(cg.num_variables);
        iota(order.begin(), order.end(), 0);
    }
    if (filter_unimportant_vars) {
        auto necessary = cg.important_vars(task.goal);
        cout << necessary.size() << " of " << order.size()
             << " variables necessary." << endl;
        vector<int> filtered;
        for (int v : order)
            if (necessary.contains(v))
                filtered.push_back(v);
        order = move(filtered);
    }
    VariableOrder vo(move(order));
    vo.apply(task);
}
}
