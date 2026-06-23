#include "axiom_rules.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../utils/sccs.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using namespace std;
namespace translate::axioms {
using namespace pddl;

string atom_key(const Atom &atom) {
    string k = atom.predicate;
    for (const auto &a : atom.args) { k.push_back('\x1f'); k += a; }
    return k;
}

namespace {
string literal_atom_key(const Literal &lit) {
    string k = lit.predicate;
    for (const auto &a : lit.args) { k.push_back('\x1f'); k += a; }
    return k;
}

struct AxiomDependencies {
    unordered_set<string> derived_variables;
    unordered_map<string, unordered_set<string>>
        positive_dependencies;
    unordered_map<string, unordered_set<string>>
        negative_dependencies;
    // Key -> representative atom (an Atom object for the derived variable).
    unordered_map<string, shared_ptr<const Atom>> repr;

    AxiomDependencies() = default;
    explicit AxiomDependencies(
        const vector<shared_ptr<PropositionalAxiom>> &axioms) {
        for (const auto &ax : axioms) {
            if (!ax || !ax->effect) continue;
            string k = atom_key(*ax->effect);
            derived_variables.insert(k);
            repr[k] = ax->effect;
        }
        for (const auto &ax : axioms) {
            if (!ax || !ax->effect) continue;
            string head = atom_key(*ax->effect);
            for (const auto &lit_cond : ax->condition) {
                if (!lit_cond) continue;
                const auto &lit = static_cast<const Literal &>(*lit_cond);
                string body_key = literal_atom_key(lit);
                if (derived_variables.contains(body_key)) {
                    if (lit.negated())
                        negative_dependencies[head].insert(body_key);
                    else
                        positive_dependencies[head].insert(body_key);
                }
            }
        }
    }

    void remove_unnecessary_variables(
        const unordered_set<string> &necessary) {
        unordered_set<string> kept;
        for (const auto &v : derived_variables) {
            if (necessary.contains(v)) kept.insert(v);
            else {
                positive_dependencies.erase(v);
                negative_dependencies.erase(v);
            }
        }
        derived_variables = move(kept);
    }
};

unordered_set<string> compute_necessary_atoms(
    const AxiomDependencies &deps,
    const vector<ConditionPtr> &goals,
    const vector<shared_ptr<PropositionalAction>> &operators) {
    unordered_set<string> necessary;
    for (const auto &g : goals) {
        if (!g) continue;
        const auto &lit = static_cast<const Literal &>(*g);
        string key = literal_atom_key(lit);
        if (deps.derived_variables.contains(key)) necessary.insert(key);
    }
    for (const auto &op : operators) {
        if (!op) continue;
        for (const auto &pre : op->precondition) {
            if (!pre) continue;
            const auto &lit = static_cast<const Literal &>(*pre);
            string key = literal_atom_key(lit);
            if (deps.derived_variables.contains(key)) necessary.insert(key);
        }
        auto walk = [&](const auto &effects) {
            for (const auto &[conds, _] : effects) {
                for (const auto &c : conds) {
                    if (!c) continue;
                    const auto &lit = static_cast<const Literal &>(*c);
                    string key = literal_atom_key(lit);
                    if (deps.derived_variables.contains(key))
                        necessary.insert(key);
                }
            }
        };
        walk(op->add_effects);
        walk(op->del_effects);
    }
    vector<string> stack(necessary.begin(), necessary.end());
    while (!stack.empty()) {
        string atom = move(stack.back());
        stack.pop_back();
        auto add = [&](const unordered_map<string,
                       unordered_set<string>> &deps_map) {
            auto it = deps_map.find(atom);
            if (it == deps_map.end()) return;
            for (const auto &body : it->second)
                if (necessary.insert(body).second) stack.push_back(body);
        };
        add(deps.positive_dependencies);
        add(deps.negative_dependencies);
    }
    return necessary;
}

vector<vector<string>> compute_sccs(
    const AxiomDependencies &deps) {
    vector<string> sorted_vars(deps.derived_variables.begin(),
                                         deps.derived_variables.end());
    ranges::sort(sorted_vars);
    unordered_map<string, int> idx;
    for (size_t i = 0; i < sorted_vars.size(); ++i)
        idx[sorted_vars[i]] = static_cast<int>(i);
    vector<vector<int>> adj(sorted_vars.size());
    for (size_t i = 0; i < sorted_vars.size(); ++i) {
        set<string> combined;
        auto add_combined = [&](const auto &m) {
            auto it = m.find(sorted_vars[i]);
            if (it == m.end()) return;
            for (const auto &v : it->second) combined.insert(v);
        };
        add_combined(deps.positive_dependencies);
        add_combined(deps.negative_dependencies);
        for (const auto &v : combined) adj[i].push_back(idx[v]);
    }
    auto idx_sccs = utils::get_sccs_adjacency_list(adj);
    vector<vector<string>> result;
    for (const auto &scc : idx_sccs) {
        vector<string> names;
        names.reserve(scc.size());
        for (int j : scc) names.push_back(sorted_vars[j]);
        result.push_back(move(names));
    }
    return result;
}

struct AxiomCluster {
    vector<string> variables;
    // For each variable in cluster, the axioms producing it.
    unordered_map<string,
                       vector<shared_ptr<PropositionalAxiom>>> axioms;
    set<int> positive_children;
    set<int> negative_children;
    int layer = 0;
};

vector<shared_ptr<PropositionalAxiom>> compute_simplified_axioms(
    vector<shared_ptr<PropositionalAxiom>> axioms) {
    if (axioms.empty()) return axioms;
    // Strict-weak order on condition literals by (predicate, args, negated).
    auto lit_less = [](const ConditionPtr &x, const ConditionPtr &y) {
        const auto &lx = static_cast<const Literal &>(*x);
        const auto &ly = static_cast<const Literal &>(*y);
        if (lx.predicate != ly.predicate) return lx.predicate < ly.predicate;
        if (lx.args != ly.args) return lx.args < ly.args;
        return lx.negated() < ly.negated();
    };
    // Deduplicate condition entries within each axiom; leaves each
    // axiom's condition sorted by `lit_less`.
    for (auto &ax : axioms) {
        vector<ConditionPtr> uniq = ax->condition;
        ranges::sort(uniq, lit_less);
        uniq.erase(ranges::begin(ranges::unique(
                       uniq, [&](const ConditionPtr &x, const ConditionPtr &y) {
                           return !lit_less(x, y) && !lit_less(y, x);
                       })),
                   uniq.end());
        ax->condition = move(uniq);
    }
    vector<bool> skip(axioms.size(), false);
    // Drop axioms whose (positive) effect atom occurs in their own condition:
    // such a rule can only fire when its head already holds, so it is
    // redundant. Matches Python's `if axiom.effect in axiom.condition` in
    // compute_simplified_axioms. These are also excluded as dominators below
    // (Python never adds them to axioms_by_literal).
    for (size_t i = 0; i < axioms.size(); ++i) {
        const auto &eff = *axioms[i]->effect;
        for (const auto &c : axioms[i]->condition) {
            const auto &l = static_cast<const Literal &>(*c);
            if (!l.negated() && l.predicate == eff.predicate &&
                l.args == eff.args) {
                skip[i] = true;
                break;
            }
        }
    }
    // Remove dominated axioms: i dominates j iff i's condition is a subset
    // of j's. Both conditions are sorted by `lit_less`, so the subset test
    // is a single linear merge via ranges::includes (O(|ci|+|cj|))
    // rather than a nested scan (O(|ci|*|cj|)). A skipped axiom never acts as
    // a dominator (matches Python skipping ids in axioms_to_skip).
    for (size_t i = 0; i < axioms.size(); ++i) {
        if (skip[i]) continue;
        for (size_t j = 0; j < axioms.size(); ++j) {
            if (i == j || skip[j]) continue;
            const auto &ci = axioms[i]->condition;
            const auto &cj = axioms[j]->condition;
            if (ci.size() > cj.size()) continue;
            if (ranges::includes(cj, ci, lit_less)) skip[j] = true;
        }
    }
    vector<shared_ptr<PropositionalAxiom>> out;
    for (size_t i = 0; i < axioms.size(); ++i)
        if (!skip[i]) out.push_back(move(axioms[i]));
    return out;
}
}

AxiomLayering handle_axioms(
    const vector<shared_ptr<PropositionalAction>> &operators,
    const vector<shared_ptr<PropositionalAxiom>> &axioms_in,
    const vector<ConditionPtr> &goals,
    const string &layer_strategy) {
    AxiomDependencies deps(axioms_in);
    auto necessary = compute_necessary_atoms(deps, goals, operators);
    deps.remove_unnecessary_variables(necessary);

    auto sccs = compute_sccs(deps);
    vector<AxiomCluster> clusters;
    clusters.reserve(sccs.size());
    unordered_map<string, int> var_to_cluster;
    for (size_t i = 0; i < sccs.size(); ++i) {
        AxiomCluster c;
        c.variables = sccs[i];
        for (const auto &v : c.variables) {
            c.axioms[v] = {};
            var_to_cluster[v] = static_cast<int>(i);
        }
        clusters.push_back(move(c));
    }
    // Assign axioms to clusters.
    for (const auto &ax : axioms_in) {
        if (!ax || !ax->effect) continue;
        string key = atom_key(*ax->effect);
        auto it = var_to_cluster.find(key);
        if (it == var_to_cluster.end()) continue;
        clusters[it->second].axioms[key].push_back(ax);
    }
    int removed = 0;
    for (auto &c : clusters) {
        for (auto &[v, ax_list] : c.axioms) {
            size_t old = ax_list.size();
            ax_list = compute_simplified_axioms(move(ax_list));
            removed += static_cast<int>(old - ax_list.size());
        }
    }
    cout << "Translator axioms removed by simplifying: " << removed
              << endl;
    // Compute inter-cluster links.
    auto add_links = [&](const unordered_map<string,
                         unordered_set<string>> &m, bool negative) {
        for (const auto &[from, deps_set] : m) {
            auto from_it = var_to_cluster.find(from);
            if (from_it == var_to_cluster.end()) continue;
            for (const auto &to : deps_set) {
                auto to_it = var_to_cluster.find(to);
                if (to_it == var_to_cluster.end()) continue;
                if (from_it->second == to_it->second) {
                    if (negative)
                        throw runtime_error(
                            "Error: The axioms are not stratifiable.");
                    continue;
                }
                if (negative)
                    clusters[from_it->second].negative_children.insert(
                        to_it->second);
                else
                    clusters[from_it->second].positive_children.insert(
                        to_it->second);
            }
        }
    };
    add_links(deps.positive_dependencies, false);
    add_links(deps.negative_dependencies, true);

    // Layer assignment, traversing clusters in reverse (topological tail
    // first).
    if (layer_strategy == "max") {
        int layer = 0;
        for (auto it = clusters.rbegin(); it != clusters.rend(); ++it)
            it->layer = layer++;
    } else {
        for (auto it = clusters.rbegin(); it != clusters.rend(); ++it) {
            int layer = 0;
            for (int child : it->positive_children)
                layer = max(layer, clusters[child].layer);
            for (int child : it->negative_children)
                layer = max(layer, clusters[child].layer + 1);
            it->layer = layer;
        }
    }

    AxiomLayering out;
    for (auto &c : clusters) {
        for (auto &v : c.variables) {
            out.axiom_layers[v] = c.layer;
            for (auto &ax : c.axioms[v]) out.axioms.push_back(move(ax));
        }
    }
    return out;
}
}
