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

namespace translate::axioms {
using namespace pddl;

std::string atom_key(const Atom &atom) {
    std::string k = atom.predicate;
    for (const auto &a : atom.args) { k.push_back('\x1f'); k += a; }
    return k;
}

namespace {
const Atom &as_atom(const Condition &c) {
    return static_cast<const Atom &>(c);
}

std::string literal_atom_key(const Literal &lit) {
    std::string k = lit.predicate;
    for (const auto &a : lit.args) { k.push_back('\x1f'); k += a; }
    return k;
}

struct AxiomDependencies {
    std::unordered_set<std::string> derived_variables;
    std::unordered_map<std::string, std::unordered_set<std::string>>
        positive_dependencies;
    std::unordered_map<std::string, std::unordered_set<std::string>>
        negative_dependencies;
    // Key -> representative atom (an Atom object for the derived variable).
    std::unordered_map<std::string, std::shared_ptr<const Atom>> repr;

    AxiomDependencies() = default;
    explicit AxiomDependencies(
        const std::vector<std::shared_ptr<PropositionalAxiom>> &axioms) {
        for (const auto &ax : axioms) {
            if (!ax || !ax->effect) continue;
            std::string k = atom_key(*ax->effect);
            derived_variables.insert(k);
            repr[k] = ax->effect;
        }
        for (const auto &ax : axioms) {
            if (!ax || !ax->effect) continue;
            std::string head = atom_key(*ax->effect);
            for (const auto &lit_cond : ax->condition) {
                if (!lit_cond) continue;
                const auto &lit = static_cast<const Literal &>(*lit_cond);
                std::string body_key = literal_atom_key(lit);
                if (derived_variables.count(body_key)) {
                    if (lit.negated())
                        negative_dependencies[head].insert(body_key);
                    else
                        positive_dependencies[head].insert(body_key);
                }
            }
        }
    }

    void remove_unnecessary_variables(
        const std::unordered_set<std::string> &necessary) {
        std::unordered_set<std::string> kept;
        for (const auto &v : derived_variables) {
            if (necessary.count(v)) kept.insert(v);
            else {
                positive_dependencies.erase(v);
                negative_dependencies.erase(v);
            }
        }
        derived_variables = std::move(kept);
    }
};

std::unordered_set<std::string> compute_necessary_atoms(
    const AxiomDependencies &deps,
    const std::vector<ConditionPtr> &goals,
    const std::vector<std::shared_ptr<PropositionalAction>> &operators) {
    std::unordered_set<std::string> necessary;
    for (const auto &g : goals) {
        if (!g) continue;
        const auto &lit = static_cast<const Literal &>(*g);
        std::string key = literal_atom_key(lit);
        if (deps.derived_variables.count(key)) necessary.insert(key);
    }
    for (const auto &op : operators) {
        if (!op) continue;
        for (const auto &pre : op->precondition) {
            if (!pre) continue;
            const auto &lit = static_cast<const Literal &>(*pre);
            std::string key = literal_atom_key(lit);
            if (deps.derived_variables.count(key)) necessary.insert(key);
        }
        auto walk = [&](const auto &effects) {
            for (const auto &[conds, _] : effects) {
                for (const auto &c : conds) {
                    if (!c) continue;
                    const auto &lit = static_cast<const Literal &>(*c);
                    std::string key = literal_atom_key(lit);
                    if (deps.derived_variables.count(key))
                        necessary.insert(key);
                }
            }
        };
        walk(op->add_effects);
        walk(op->del_effects);
    }
    std::vector<std::string> stack(necessary.begin(), necessary.end());
    while (!stack.empty()) {
        std::string atom = std::move(stack.back());
        stack.pop_back();
        auto add = [&](const std::unordered_map<std::string,
                       std::unordered_set<std::string>> &deps_map) {
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

std::vector<std::vector<std::string>> compute_sccs(
    const AxiomDependencies &deps) {
    std::vector<std::string> sorted_vars(deps.derived_variables.begin(),
                                         deps.derived_variables.end());
    std::sort(sorted_vars.begin(), sorted_vars.end());
    std::unordered_map<std::string, int> idx;
    for (std::size_t i = 0; i < sorted_vars.size(); ++i)
        idx[sorted_vars[i]] = static_cast<int>(i);
    std::vector<std::vector<int>> adj(sorted_vars.size());
    for (std::size_t i = 0; i < sorted_vars.size(); ++i) {
        std::set<std::string> combined;
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
    std::vector<std::vector<std::string>> result;
    for (const auto &scc : idx_sccs) {
        std::vector<std::string> names;
        names.reserve(scc.size());
        for (int j : scc) names.push_back(sorted_vars[j]);
        result.push_back(std::move(names));
    }
    return result;
}

struct AxiomCluster {
    std::vector<std::string> variables;
    // For each variable in cluster, the axioms producing it.
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<PropositionalAxiom>>> axioms;
    std::set<int> positive_children;
    std::set<int> negative_children;
    int layer = 0;
};

bool less_than_axiom(const PropositionalAxiom &a, const PropositionalAxiom &b) {
    if (a.name != b.name) return a.name < b.name;
    if (a.condition.size() != b.condition.size())
        return a.condition.size() < b.condition.size();
    for (std::size_t i = 0; i < a.condition.size(); ++i) {
        const auto &la = static_cast<const Literal &>(*a.condition[i]);
        const auto &lb = static_cast<const Literal &>(*b.condition[i]);
        if (la.predicate != lb.predicate) return la.predicate < lb.predicate;
        if (la.args != lb.args) return la.args < lb.args;
        if (la.negated() != lb.negated()) return la.negated() < lb.negated();
    }
    if (a.effect && b.effect) {
        if (a.effect->predicate != b.effect->predicate)
            return a.effect->predicate < b.effect->predicate;
        return a.effect->args < b.effect->args;
    }
    return false;
}

std::vector<std::shared_ptr<PropositionalAxiom>> compute_simplified_axioms(
    std::vector<std::shared_ptr<PropositionalAxiom>> axioms) {
    if (axioms.empty()) return axioms;
    // Deduplicate condition entries within each axiom.
    for (auto &ax : axioms) {
        std::vector<ConditionPtr> uniq = ax->condition;
        std::sort(uniq.begin(), uniq.end(),
                  [](const ConditionPtr &x, const ConditionPtr &y) {
                      const auto &lx = static_cast<const Literal &>(*x);
                      const auto &ly = static_cast<const Literal &>(*y);
                      if (lx.predicate != ly.predicate)
                          return lx.predicate < ly.predicate;
                      if (lx.args != ly.args) return lx.args < ly.args;
                      return lx.negated() < ly.negated();
                  });
        uniq.erase(std::unique(uniq.begin(), uniq.end(),
                               [](const ConditionPtr &x,
                                  const ConditionPtr &y) {
                                   const auto &lx =
                                       static_cast<const Literal &>(*x);
                                   const auto &ly =
                                       static_cast<const Literal &>(*y);
                                   return lx.predicate == ly.predicate &&
                                          lx.args == ly.args &&
                                          lx.negated() == ly.negated();
                               }),
                   uniq.end());
        ax->condition = std::move(uniq);
    }
    // Remove dominated axioms (naive O(n^2)).
    std::vector<bool> skip(axioms.size(), false);
    for (std::size_t i = 0; i < axioms.size(); ++i) {
        for (std::size_t j = 0; j < axioms.size(); ++j) {
            if (i == j || skip[j]) continue;
            // i dominates j iff i's condition is a subset of j's condition.
            const auto &ci = axioms[i]->condition;
            const auto &cj = axioms[j]->condition;
            if (ci.size() > cj.size()) continue;
            bool subset = true;
            for (const auto &lit_i : ci) {
                bool found = false;
                const auto &li = static_cast<const Literal &>(*lit_i);
                for (const auto &lit_j : cj) {
                    const auto &lj = static_cast<const Literal &>(*lit_j);
                    if (li.predicate == lj.predicate &&
                        li.args == lj.args &&
                        li.negated() == lj.negated()) {
                        found = true; break;
                    }
                }
                if (!found) { subset = false; break; }
            }
            if (subset) skip[j] = true;
        }
    }
    std::vector<std::shared_ptr<PropositionalAxiom>> out;
    for (std::size_t i = 0; i < axioms.size(); ++i)
        if (!skip[i]) out.push_back(std::move(axioms[i]));
    return out;
}
}

AxiomLayering handle_axioms(
    const std::vector<std::shared_ptr<PropositionalAction>> &operators,
    const std::vector<std::shared_ptr<PropositionalAxiom>> &axioms_in,
    const std::vector<ConditionPtr> &goals,
    const std::string &layer_strategy) {
    AxiomDependencies deps(axioms_in);
    auto necessary = compute_necessary_atoms(deps, goals, operators);
    deps.remove_unnecessary_variables(necessary);

    auto sccs = compute_sccs(deps);
    std::vector<AxiomCluster> clusters;
    clusters.reserve(sccs.size());
    std::unordered_map<std::string, int> var_to_cluster;
    for (std::size_t i = 0; i < sccs.size(); ++i) {
        AxiomCluster c;
        c.variables = sccs[i];
        for (const auto &v : c.variables) {
            c.axioms[v] = {};
            var_to_cluster[v] = static_cast<int>(i);
        }
        clusters.push_back(std::move(c));
    }
    // Assign axioms to clusters.
    for (const auto &ax : axioms_in) {
        if (!ax || !ax->effect) continue;
        std::string key = atom_key(*ax->effect);
        auto it = var_to_cluster.find(key);
        if (it == var_to_cluster.end()) continue;
        clusters[it->second].axioms[key].push_back(ax);
    }
    int removed = 0;
    for (auto &c : clusters) {
        for (auto &[v, ax_list] : c.axioms) {
            std::size_t old = ax_list.size();
            ax_list = compute_simplified_axioms(std::move(ax_list));
            removed += static_cast<int>(old - ax_list.size());
        }
    }
    std::cout << "Translator axioms removed by simplifying: " << removed
              << std::endl;
    // Compute inter-cluster links.
    auto add_links = [&](const std::unordered_map<std::string,
                         std::unordered_set<std::string>> &m, bool negative) {
        for (const auto &[from, deps_set] : m) {
            auto from_it = var_to_cluster.find(from);
            if (from_it == var_to_cluster.end()) continue;
            for (const auto &to : deps_set) {
                auto to_it = var_to_cluster.find(to);
                if (to_it == var_to_cluster.end()) continue;
                if (from_it->second == to_it->second) {
                    if (negative)
                        throw std::runtime_error(
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
                layer = std::max(layer, clusters[child].layer);
            for (int child : it->negative_children)
                layer = std::max(layer, clusters[child].layer + 1);
            it->layer = layer;
        }
    }

    AxiomLayering out;
    for (auto &c : clusters) {
        for (auto &v : c.variables) {
            out.axiom_layers[v] = c.layer;
            for (auto &ax : c.axioms[v]) out.axioms.push_back(std::move(ax));
        }
    }
    return out;
}
}
