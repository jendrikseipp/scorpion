#include "axiom_rules.h"

#include "../grounding/symbols.h"
#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../utils/sccs.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
namespace translate::axioms {
using namespace pddl;

namespace {
/*
  Sign-independent identity of a (positive or negated) literal, used to key the
  derived-variable dependency graph. The predicate id is cached on every Literal
  and object names were interned during grounding, so this is the same interned
  GroundKey the rest of the translator uses -- membership and dependency lookups
  become int hash/compare instead of hashing and memcmp-ing predicate/argument
  strings (the dominant cost of axiom processing on derived-predicate-heavy
  tasks). The only place that needs the *name* order is the derived-variable
  sort in compute_sccs, which sorts via the representative Atom (see there).
*/
using AtomKey = GroundKey;
// Hashed set/map over AtomKeys, the workhorse containers of this file.
using KeySet = unordered_set<AtomKey, GroundKeyHash>;
template<typename V>
using KeyMap = unordered_map<AtomKey, V, GroundKeyHash>;

AtomKey atom_key(const Literal &lit) {
    AtomKey key;
    key.predicate = lit.predicate_id;
    key.args.reserve(lit.args.size());
    for (const auto &a : lit.args)
        key.args.push_back(grounding::symbols().intern(a));
    return key;
}

// Order two derived variables by their representative atom's (predicate, args)
// names -- the lexicographic order the translator has always emitted derived
// variables in. GroundKey's own ordering is by interned id, which is not name
// order, so the byte-critical sort in compute_sccs routes through this instead.
bool atom_name_less(const Atom &a, const Atom &b) {
    if (a.predicate != b.predicate)
        return a.predicate < b.predicate;
    return a.args < b.args;
}

struct AxiomDependencies {
    KeySet derived_variables;
    KeyMap<KeySet> positive_dependencies;
    KeyMap<KeySet> negative_dependencies;
    // Key -> representative atom (an Atom object for the derived variable).
    KeyMap<shared_ptr<const Atom>> repr;

    AxiomDependencies() = default;
    explicit AxiomDependencies(
        const vector<shared_ptr<PropositionalAxiom>> &axioms) {
        for (const auto &ax : axioms) {
            if (!ax || !ax->effect)
                continue;
            AtomKey k = atom_key(*ax->effect);
            derived_variables.insert(k);
            repr[k] = ax->effect;
        }
        for (const auto &ax : axioms) {
            if (!ax || !ax->effect)
                continue;
            AtomKey head = atom_key(*ax->effect);
            for (const auto &lit_cond : ax->condition) {
                if (!lit_cond)
                    continue;
                const auto &lit = static_cast<const Literal &>(*lit_cond);
                AtomKey body_key = atom_key(lit);
                if (derived_variables.contains(body_key)) {
                    if (lit.negated())
                        negative_dependencies[head].insert(body_key);
                    else
                        positive_dependencies[head].insert(body_key);
                }
            }
        }
    }

    void remove_unnecessary_variables(const KeySet &necessary) {
        KeySet kept;
        for (const auto &v : derived_variables) {
            if (necessary.contains(v))
                kept.insert(v);
            else {
                positive_dependencies.erase(v);
                negative_dependencies.erase(v);
            }
        }
        derived_variables = move(kept);
    }
};

KeySet compute_necessary_atoms(
    const AxiomDependencies &deps, const vector<ConditionPtr> &goals,
    const vector<PropositionalAction> &operators,
    const vector<shared_ptr<const Atom>> &fact_by_id) {
    KeySet necessary;
    // Without derived predicates nothing is necessary -- and skipping here
    // avoids scanning every action's literals on the common axiom-free tasks.
    if (deps.derived_variables.empty())
        return necessary;
    for (const auto &g : goals) {
        if (!g)
            continue;
        const auto &lit = static_cast<const Literal &>(*g);
        AtomKey key = atom_key(lit);
        if (deps.derived_variables.contains(key))
            necessary.insert(key);
    }
    // Action literals are GroundLiterals; recover the atom key via fact_by_id.
    auto check = [&](const GroundLiteral &gl) {
        AtomKey key = atom_key(*fact_by_id[gl.fact]);
        if (deps.derived_variables.contains(key))
            necessary.insert(key);
    };
    for (const auto &op : operators) {
        for (const auto &pre : op.precondition)
            check(pre);
        auto walk = [&](const auto &effects) {
            for (const auto &[conds, _] : effects)
                for (const auto &c : conds)
                    check(c);
        };
        walk(op.add_effects);
        walk(op.del_effects);
    }
    vector<AtomKey> stack(necessary.begin(), necessary.end());
    while (!stack.empty()) {
        AtomKey atom = move(stack.back());
        stack.pop_back();
        auto add = [&](const KeyMap<KeySet> &deps_map) {
            auto it = deps_map.find(atom);
            if (it == deps_map.end())
                return;
            for (const auto &body : it->second)
                if (necessary.insert(body).second)
                    stack.push_back(body);
        };
        add(deps.positive_dependencies);
        add(deps.negative_dependencies);
    }
    return necessary;
}

vector<vector<AtomKey>> compute_sccs(const AxiomDependencies &deps) {
    vector<AtomKey> sorted_vars(
        deps.derived_variables.begin(), deps.derived_variables.end());
    ranges::sort(sorted_vars, [&deps](const AtomKey &a, const AtomKey &b) {
        return atom_name_less(*deps.repr.at(a), *deps.repr.at(b));
    });
    KeyMap<int> idx;
    for (size_t i = 0; i < sorted_vars.size(); ++i)
        idx[sorted_vars[i]] = static_cast<int>(i);
    vector<vector<int>> adj(sorted_vars.size());
    for (size_t i = 0; i < sorted_vars.size(); ++i) {
        // Neighbours are this variable's positive+negative dependencies, as
        // ascending sorted_vars indices. A dependency of a necessary variable
        // is itself necessary (compute_necessary_atoms closes over
        // dependencies), so every referenced variable is in idx.
        // Sorting+deduping the indices matches the old name-ordered
        // set<AtomKey> (sorted_vars is name-sorted, so index order == name
        // order) without hashing string keys.
        vector<int> &nbrs = adj[i];
        auto add_neighbors = [&](const auto &m) {
            auto it = m.find(sorted_vars[i]);
            if (it == m.end())
                return;
            for (const auto &v : it->second)
                nbrs.push_back(idx.at(v));
        };
        add_neighbors(deps.positive_dependencies);
        add_neighbors(deps.negative_dependencies);
        ranges::sort(nbrs);
        nbrs.erase(ranges::begin(ranges::unique(nbrs)), nbrs.end());
    }
    auto idx_sccs = utils::get_sccs_adjacency_list(adj);
    vector<vector<AtomKey>> result;
    for (const auto &scc : idx_sccs) {
        vector<AtomKey> names;
        names.reserve(scc.size());
        for (int j : scc)
            names.push_back(sorted_vars[j]);
        result.push_back(move(names));
    }
    return result;
}

struct AxiomCluster {
    vector<AtomKey> variables;
    // For each variable in cluster, the axioms producing it.
    KeyMap<vector<shared_ptr<PropositionalAxiom>>> axioms;
    set<int> positive_children;
    set<int> negative_children;
    int layer = 0;
};

vector<shared_ptr<PropositionalAxiom>> compute_simplified_axioms(
    vector<shared_ptr<PropositionalAxiom>> axioms) {
    if (axioms.empty())
        return axioms;
    // Strict-weak order on condition literals by (predicate, args, negated).
    auto lit_less = [](const ConditionPtr &x, const ConditionPtr &y) {
        const auto &lx = static_cast<const Literal &>(*x);
        const auto &ly = static_cast<const Literal &>(*y);
        if (lx.predicate != ly.predicate)
            return lx.predicate < ly.predicate;
        if (lx.args != ly.args)
            return lx.args < ly.args;
        return lx.negated() < ly.negated();
    };
    // Deduplicate condition entries within each axiom; leaves each
    // axiom's condition sorted by `lit_less`.
    for (auto &ax : axioms) {
        vector<ConditionPtr> uniq = ax->condition;
        ranges::sort(uniq, lit_less);
        uniq.erase(
            ranges::begin(ranges::unique(
                uniq,
                [&](const ConditionPtr &x, const ConditionPtr &y) {
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
        if (skip[i])
            continue;
        for (size_t j = 0; j < axioms.size(); ++j) {
            if (i == j || skip[j])
                continue;
            const auto &ci = axioms[i]->condition;
            const auto &cj = axioms[j]->condition;
            if (ci.size() > cj.size())
                continue;
            if (ranges::includes(cj, ci, lit_less))
                skip[j] = true;
        }
    }
    vector<shared_ptr<PropositionalAxiom>> out;
    for (size_t i = 0; i < axioms.size(); ++i)
        if (!skip[i])
            out.push_back(move(axioms[i]));
    return out;
}

// Assign each axiom cluster a layer, traversing clusters in reverse
// (topological tail first). "max" gives every cluster its own layer; the
// default packs clusters into the fewest layers the dependencies allow (a
// negated dependency forces the dependent one layer higher).
void assign_axiom_layers(
    vector<AxiomCluster> &clusters, const string &layer_strategy) {
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
}
}

AxiomLayering handle_axioms(
    const vector<PropositionalAction> &operators,
    const vector<shared_ptr<PropositionalAxiom>> &axioms_in,
    const vector<ConditionPtr> &goals,
    const vector<shared_ptr<const Atom>> &fact_by_id,
    const string &layer_strategy) {
    AxiomDependencies deps(axioms_in);
    auto necessary =
        compute_necessary_atoms(deps, goals, operators, fact_by_id);
    deps.remove_unnecessary_variables(necessary);

    auto sccs = compute_sccs(deps);
    vector<AxiomCluster> clusters;
    clusters.reserve(sccs.size());
    KeyMap<int> var_to_cluster;
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
        if (!ax || !ax->effect)
            continue;
        AtomKey key = atom_key(*ax->effect);
        auto it = var_to_cluster.find(key);
        if (it == var_to_cluster.end())
            continue;
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
    cout << "Translator axioms removed by simplifying: " << removed << endl;
    // Compute inter-cluster links.
    auto add_links = [&](const KeyMap<KeySet> &m, bool negative) {
        for (const auto &[from, deps_set] : m) {
            auto from_it = var_to_cluster.find(from);
            if (from_it == var_to_cluster.end())
                continue;
            for (const auto &to : deps_set) {
                auto to_it = var_to_cluster.find(to);
                if (to_it == var_to_cluster.end())
                    continue;
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

    assign_axiom_layers(clusters, layer_strategy);

    AxiomLayering out;
    for (auto &c : clusters) {
        for (auto &v : c.variables) {
            out.axiom_layers.push_back({deps.repr.at(v), c.layer});
            for (auto &ax : c.axioms[v])
                out.axioms.push_back(move(ax));
        }
    }
    return out;
}
}
