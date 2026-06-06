#include "variable_order.h"

#include "../translate_options.h"
#include "../utils/sccs.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace translate::simplify {
using namespace sas;

namespace {
class CausalGraph {
public:
    std::vector<std::map<int, int>> weighted_graph; // src -> tgt -> weight
    std::vector<std::set<int>> predecessor_graph;
    int num_variables;
    std::unordered_map<int, int> goal_map;

    explicit CausalGraph(const SASTask &task) {
        num_variables = static_cast<int>(task.variables.ranges.size());
        weighted_graph.assign(num_variables, {});
        predecessor_graph.assign(num_variables, {});
        for (const auto &[v, val] : task.goal.pairs) goal_map[v] = val;
        weight_from_ops(task.operators);
        weight_from_axioms(task.axioms);
    }

    void weight_from_ops(const std::vector<SASOperator> &operators) {
        for (const auto &op : operators) {
            std::vector<int> source_vars;
            for (const auto &[v, _] : op.prevail) source_vars.push_back(v);
            for (const auto &[v, pre, post, cond] : op.pre_post)
                if (pre != -1) source_vars.push_back(v);
            for (const auto &[tgt, pre, post, cond] : op.pre_post) {
                auto extra = source_vars;
                for (const auto &[cv, cval] : cond) extra.push_back(cv);
                for (int src : extra) {
                    if (src != tgt) {
                        ++weighted_graph[src][tgt];
                        predecessor_graph[tgt].insert(src);
                    }
                }
            }
        }
    }

    void weight_from_axioms(const std::vector<SASAxiom> &axioms) {
        for (const auto &ax : axioms) {
            int tgt = ax.effect.first;
            for (const auto &[src, _] : ax.condition) {
                if (src != tgt) {
                    ++weighted_graph[src][tgt];
                    predecessor_graph[tgt].insert(src);
                }
            }
        }
    }

    std::vector<std::vector<int>> get_sccs() const {
        std::vector<std::vector<int>> adj(num_variables);
        for (int s = 0; s < num_variables; ++s)
            for (const auto &[t, _] : weighted_graph[s]) adj[s].push_back(t);
        for (auto &v : adj) std::sort(v.begin(), v.end());
        return utils::get_sccs_adjacency_list(adj);
    }

    std::vector<int> get_ordering() const {
        auto sccs = get_sccs();
        std::vector<int> order;
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
            std::unordered_set<int> scc_set(scc.begin(), scc.end());
            std::unordered_map<int, std::vector<std::pair<int, int>>>
                subgraph;
            for (int var : scc) {
                auto &edges = subgraph[var];
                // weighted_graph[var] is a std::map<int,int> -> already
                // sorted by target id, matching Python's
                // sorted(items()).
                for (const auto &[tgt, cost] : weighted_graph[var]) {
                    if (!scc_set.contains(tgt)) continue;
                    if (goal_map.contains(tgt))
                        edges.emplace_back(tgt, 100000 + cost);
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
    static std::vector<int> max_dag_order(
        const std::unordered_map<int, std::vector<std::pair<int, int>>>
            &subgraph,
        const std::vector<int> &input_order) {
        std::unordered_map<int, int> incoming_weights;
        for (const auto &[_src, edges] : subgraph) {
            for (const auto &[tgt, w] : edges)
                incoming_weights[tgt] += w;
        }

        // weight -> nodes with that current incoming weight, FIFO in
        // input order.
        std::unordered_map<int, std::deque<int>> weight_to_nodes;
        for (int node : input_order) {
            int w = incoming_weights[node]; // 0 if not seen
            weight_to_nodes[w].push_back(node);
        }

        // Min-heap of distinct weight values (lazy deletion: we never
        // remove eagerly, only the bucket-empty case).
        std::priority_queue<int, std::vector<int>, std::greater<int>>
            weights;
        {
            std::unordered_set<int> seen;
            for (const auto &[w, _] : weight_to_nodes)
                if (seen.insert(w).second) weights.push(w);
        }

        std::unordered_set<int> done;
        std::vector<int> result;
        result.reserve(input_order.size());
        while (!weights.empty()) {
            int min_key = weights.top();
            // `weight_to_nodes[min_key]` may create an empty deque if
            // min_key was previously erased -- that's intentional and
            // matches Python's defaultdict.
            auto &entries = weight_to_nodes[min_key];
            int min_elem = -1;
            bool elem_found = false;
            while (!entries.empty() &&
                   (!elem_found || done.contains(min_elem) ||
                    min_key > incoming_weights[min_elem])) {
                min_elem = entries.front();
                entries.pop_front();
                elem_found = true;
            }
            if (entries.empty()) {
                weight_to_nodes.erase(min_key);
                weights.pop();
            }
            if (!elem_found || done.contains(min_elem) ||
                min_key > incoming_weights[min_elem]) {
                continue;
            }

            done.insert(min_elem);
            result.push_back(min_elem);
            auto sit = subgraph.find(min_elem);
            if (sit == subgraph.end()) continue;
            for (const auto &[target, w] : sit->second) {
                if (done.contains(target)) continue;
                int decrement = w % 100000;
                if (decrement == 0) continue;
                int old_iw = incoming_weights[target];
                int new_iw = old_iw - decrement;
                incoming_weights[target] = new_iw;
                // Lazy heap entry: only push the weight if no bucket
                // yet exists for it.
                if (weight_to_nodes.find(new_iw) == weight_to_nodes.end())
                    weights.push(new_iw);
                weight_to_nodes[new_iw].push_back(target);
            }
        }
        return result;
    }

    std::unordered_set<int> important_vars(const SASGoal &goal) const {
        std::unordered_set<int> necessary;
        std::vector<int> stack;
        for (const auto &[v, _] : goal.pairs) {
            if (necessary.insert(v).second) stack.push_back(v);
        }
        while (!stack.empty()) {
            int n = stack.back(); stack.pop_back();
            for (int pred : predecessor_graph[n])
                if (necessary.insert(pred).second) stack.push_back(pred);
        }
        return necessary;
    }
};

class VariableOrder {
public:
    std::vector<int> ordering;
    std::unordered_map<int, int> new_var;

    explicit VariableOrder(std::vector<int> ord) : ordering(std::move(ord)) {
        for (int i = 0; i < static_cast<int>(ordering.size()); ++i)
            new_var[ordering[i]] = i;
    }

    void apply(SASTask &task) const {
        // Variables.
        std::vector<int> ranges, layers;
        std::vector<std::vector<std::string>> names;
        for (int var : ordering) {
            ranges.push_back(task.variables.ranges[var]);
            layers.push_back(task.variables.axiom_layers[var]);
            names.push_back(task.variables.value_names[var]);
        }
        task.variables.ranges = std::move(ranges);
        task.variables.axiom_layers = std::move(layers);
        task.variables.value_names = std::move(names);
        // Init.
        std::vector<int> new_init;
        for (int var : ordering) new_init.push_back(task.init.values[var]);
        task.init.values = std::move(new_init);
        // Goal.
        std::vector<VarVal> new_goal;
        for (const auto &[v, val] : task.goal.pairs) {
            auto it = new_var.find(v);
            if (it != new_var.end()) new_goal.emplace_back(it->second, val);
        }
        std::sort(new_goal.begin(), new_goal.end());
        task.goal.pairs = std::move(new_goal);
        // Mutexes.
        std::vector<SASMutexGroup> new_mutexes;
        for (auto &m : task.mutexes) {
            std::vector<VarVal> facts;
            std::set<int> vars;
            for (const auto &[v, val] : m.facts) {
                auto it = new_var.find(v);
                if (it != new_var.end()) {
                    facts.emplace_back(it->second, val);
                    vars.insert(it->second);
                }
            }
            if (vars.size() > 1) {
                m.facts = std::move(facts);
                new_mutexes.push_back(std::move(m));
            }
        }
        std::cout << new_mutexes.size() << " of " << task.mutexes.size()
                  << " mutex groups necessary." << std::endl;
        task.mutexes = std::move(new_mutexes);
        // Operators.
        std::vector<SASOperator> new_ops;
        for (auto &op : task.operators) {
            std::vector<std::tuple<int, int, int, std::vector<VarVal>>>
                new_pre_post;
            for (auto &[v, pre, post, cond] : op.pre_post) {
                auto it = new_var.find(v);
                if (it == new_var.end()) continue;
                std::vector<VarVal> new_cond;
                for (const auto &[cv, cval] : cond) {
                    auto cit = new_var.find(cv);
                    if (cit != new_var.end())
                        new_cond.emplace_back(cit->second, cval);
                }
                new_pre_post.emplace_back(it->second, pre, post,
                                          std::move(new_cond));
            }
            if (new_pre_post.empty() && !get_options().keep_no_ops) continue;
            std::vector<VarVal> new_prevail;
            for (const auto &[v, val] : op.prevail) {
                auto it = new_var.find(v);
                if (it != new_var.end())
                    new_prevail.emplace_back(it->second, val);
            }
            op.prevail = std::move(new_prevail);
            op.pre_post = std::move(new_pre_post);
            new_ops.push_back(std::move(op));
        }
        std::cout << new_ops.size() << " of " << task.operators.size()
                  << " operators necessary." << std::endl;
        task.operators = std::move(new_ops);
        // Axioms.
        std::vector<SASAxiom> new_ax;
        for (auto &ax : task.axioms) {
            auto it = new_var.find(ax.effect.first);
            if (it == new_var.end()) continue;
            std::vector<VarVal> new_cond;
            for (const auto &[v, val] : ax.condition) {
                auto cit = new_var.find(v);
                if (cit != new_var.end())
                    new_cond.emplace_back(cit->second, val);
            }
            ax.condition = std::move(new_cond);
            ax.effect = {it->second, ax.effect.second};
            new_ax.push_back(std::move(ax));
        }
        std::cout << new_ax.size() << " of " << task.axioms.size()
                  << " axiom rules necessary." << std::endl;
        task.axioms = std::move(new_ax);
    }
};
}

void find_and_apply_variable_order(SASTask &task, bool reorder_vars,
                                   bool filter_unimportant_vars) {
    if (!reorder_vars && !filter_unimportant_vars) return;
    CausalGraph cg(task);
    std::vector<int> order;
    if (reorder_vars) order = cg.get_ordering();
    else for (int i = 0; i < cg.num_variables; ++i) order.push_back(i);
    if (filter_unimportant_vars) {
        auto necessary = cg.important_vars(task.goal);
        std::cout << necessary.size() << " of " << order.size()
                  << " variables necessary." << std::endl;
        std::vector<int> filtered;
        for (int v : order) if (necessary.contains(v)) filtered.push_back(v);
        order = std::move(filtered);
    }
    VariableOrder vo(std::move(order));
    vo.apply(task);
}
}
