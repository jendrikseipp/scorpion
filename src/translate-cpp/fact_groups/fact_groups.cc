#include "fact_groups.h"

#include "../invariants/invariant_finder.h"
#include "../translate_options.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace translate::fact_groups {
using namespace pddl;

namespace {
int find_placeholder(const Atom &atom) {
    for (std::size_t i = 0; i < atom.args.size(); ++i)
        if (atom.args[i] == "?X") return static_cast<int>(i);
    return -1;
}

std::vector<ConditionPtr> expand_group(
    const std::vector<ConditionPtr> &group, const Task &task,
    const AtomSet &reachable_facts) {
    std::vector<ConditionPtr> result;
    for (const auto &fact : group) {
        if (!fact || fact->kind() != Condition::Kind::ATOM) continue;
        const auto &atom = static_cast<const Atom &>(*fact);
        int pos = find_placeholder(atom);
        if (pos < 0) {
            if (reachable_facts.count(fact)) result.push_back(fact);
        } else {
            for (const auto &obj : task.objects) {
                auto new_args = atom.args;
                new_args[pos] = obj.name;
                auto candidate = std::make_shared<const Atom>(
                    atom.predicate, std::move(new_args));
                if (reachable_facts.count(candidate))
                    result.push_back(candidate);
            }
        }
    }
    return result;
}

std::vector<std::vector<ConditionPtr>> instantiate_groups(
    const std::vector<std::vector<ConditionPtr>> &groups, const Task &task,
    const AtomSet &reachable_facts) {
    std::vector<std::vector<ConditionPtr>> result;
    result.reserve(groups.size());
    for (const auto &g : groups)
        result.push_back(expand_group(g, task, reachable_facts));
    return result;
}

std::string atom_to_string(const ConditionPtr &c) {
    if (!c) return "";
    const auto &lit = static_cast<const Literal &>(*c);
    std::ostringstream os;
    if (lit.negated()) os << "Negated";
    os << "Atom " << lit.predicate << "(";
    for (std::size_t i = 0; i < lit.args.size(); ++i) {
        if (i) os << ", ";
        os << lit.args[i];
    }
    os << ")";
    return os.str();
}

bool atom_less(const ConditionPtr &a, const ConditionPtr &b) {
    if (!a || !b) return a.get() < b.get();
    const auto &la = static_cast<const Literal &>(*a);
    const auto &lb = static_cast<const Literal &>(*b);
    if (la.predicate != lb.predicate) return la.predicate < lb.predicate;
    return la.args < lb.args;
}

std::vector<std::vector<ConditionPtr>> sort_groups(
    std::vector<std::vector<ConditionPtr>> groups) {
    for (auto &g : groups) std::sort(g.begin(), g.end(), atom_less);
    std::sort(groups.begin(), groups.end(),
              [](const std::vector<ConditionPtr> &a,
                 const std::vector<ConditionPtr> &b) {
                  return std::lexicographical_compare(
                      a.begin(), a.end(), b.begin(), b.end(), atom_less);
              });
    return groups;
}

std::vector<std::vector<ConditionPtr>> collect_all_mutex_groups(
    const std::vector<std::vector<ConditionPtr>> &groups,
    const AtomSet &atoms) {
    std::vector<std::vector<ConditionPtr>> result;
    AtomSet uncovered = atoms;
    for (const auto &g : groups) {
        for (const auto &a : g) uncovered.erase(a);
        result.push_back(g);
    }
    std::vector<ConditionPtr> remaining(uncovered.begin(), uncovered.end());
    std::sort(remaining.begin(), remaining.end(), atom_less);
    for (const auto &a : remaining)
        result.push_back({a});
    return result;
}

std::vector<std::vector<ConditionPtr>> choose_groups(
    const std::vector<std::vector<ConditionPtr>> &groups_in,
    const AtomSet &atoms, const AtomSet &negative_in_goal) {
    // Optionally remove negative-in-goal atoms.
    std::vector<std::vector<ConditionPtr>> groups;
    groups.reserve(groups_in.size());
    for (const auto &g : groups_in) {
        std::vector<ConditionPtr> filtered;
        for (const auto &a : g)
            if (!negative_in_goal.count(a)) filtered.push_back(a);
        groups.push_back(std::move(filtered));
    }
    // Greedy set-cover by largest group first.
    std::vector<std::size_t> remaining_size(groups.size());
    for (std::size_t i = 0; i < groups.size(); ++i)
        remaining_size[i] = groups[i].size();
    std::vector<std::vector<ConditionPtr>> result;
    AtomSet uncovered = atoms;

    bool use_partial = get_options().use_partial_encoding;
    while (true) {
        std::size_t best = 0;
        bool found_any = false;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (remaining_size[i] > remaining_size[best]) { best = i; found_any = true; }
            else if (!found_any && remaining_size[i] > 1) {
                best = i; found_any = true;
            }
        }
        if (!found_any || remaining_size[best] < 2) break;
        // Materialize the chosen group from the remaining set.
        std::vector<ConditionPtr> chosen;
        for (const auto &a : groups[best])
            if (a) chosen.push_back(a);
        // Mark used facts and (if partial encoding) remove them from
        // other groups.
        if (use_partial) {
            for (const auto &a : chosen) {
                for (std::size_t j = 0; j < groups.size(); ++j) {
                    if (j == best) continue;
                    auto it = std::remove(groups[j].begin(),
                                           groups[j].end(), a);
                    if (it != groups[j].end()) {
                        remaining_size[j] -= std::distance(it, groups[j].end());
                        groups[j].erase(it, groups[j].end());
                    }
                }
            }
        }
        // Update uncovered with the chosen facts.
        for (const auto &a : chosen) uncovered.erase(a);
        groups[best].clear();
        remaining_size[best] = 0;
        result.push_back(std::move(chosen));
    }
    std::cout << uncovered.size() << " uncovered facts" << std::endl;
    std::vector<ConditionPtr> rem(uncovered.begin(), uncovered.end());
    std::sort(rem.begin(), rem.end(), atom_less);
    for (const auto &a : rem) result.push_back({a});
    return result;
}

std::vector<std::vector<std::string>> build_translation_key(
    const std::vector<std::vector<ConditionPtr>> &groups) {
    std::vector<std::vector<std::string>> keys;
    keys.reserve(groups.size());
    for (const auto &g : groups) {
        std::vector<std::string> key;
        for (const auto &f : g) key.push_back(atom_to_string(f));
        if (g.size() == 1) {
            const auto &lit = static_cast<const Literal &>(*g[0]);
            auto neg = lit.negate();
            key.push_back(atom_to_string(neg));
        } else {
            key.push_back("<none of those>");
        }
        keys.push_back(std::move(key));
    }
    return keys;
}
}

ComputedGroups compute_groups(
    const Task &task, const AtomSet &atoms,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters,
    const AtomSet &negative_in_goal) {
    auto raw = invariants::get_groups(task, reachable_action_parameters);
    auto instantiated = instantiate_groups(raw, task, atoms);
    auto sorted = sort_groups(std::move(instantiated));
    ComputedGroups out;
    out.mutex_groups = collect_all_mutex_groups(sorted, atoms);
    out.groups = sort_groups(choose_groups(sorted, atoms, negative_in_goal));
    out.translation_key = build_translation_key(out.groups);
    return out;
}
}
