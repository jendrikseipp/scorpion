#include "fact_groups.h"

#include "../invariants/invariant_finder.h"
#include "../translate_options.h"
#include "../utils/timer.h"

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

    const int n = static_cast<int>(groups.size());
    const bool use_partial = get_options().use_partial_encoding;

    // Greedy set-cover. We avoid the O(N^2) std::remove_if per pick by
    // maintaining an atom->containing-groups index and per-group counters
    // of uncovered atoms. Picking a group marks its uncovered atoms as
    // covered and (under partial encoding) decrements the counters of
    // every other containing group in O(1) per affected (atom, group)
    // pair instead of scanning each group.
    std::unordered_map<ConditionPtr, std::vector<int>,
                       ConditionPtrHash, ConditionPtrEqual> atom_to_groups;
    if (use_partial) {
        for (int i = 0; i < n; ++i)
            for (const auto &a : groups[i])
                atom_to_groups[a].push_back(i);
    }

    std::vector<int> remaining(n);
    for (int i = 0; i < n; ++i)
        remaining[i] = static_cast<int>(groups[i].size());

    AtomSet covered;
    std::vector<std::vector<ConditionPtr>> result;

    while (true) {
        int best = -1;
        int best_size = 1; // we only pick multi-element groups
        /*
          Tie-breaking direction matters: Python's GroupCoverQueue pops
          from the back of `groups_by_size[max_size]` (LIFO over the
          input order). Use `>=` here so that among ties, the *last*
          index wins -- matches Python's "pop from end" behaviour and
          flips us from cpp's previous "first-of-tied" picking. On
          blocks/probBLOCKS-4-0 this single-character change makes
          cpp pick the "where is X" mutex grouping that Python prefers
          rather than "what's on top of X".
        */
        for (int i = 0; i < n; ++i) {
            if (remaining[i] >= best_size && remaining[i] > 1) {
                best = i; best_size = remaining[i];
            }
        }
        if (best < 0) break;

        std::vector<ConditionPtr> chosen;
        chosen.reserve(best_size);
        for (const auto &a : groups[best]) {
            if (covered.find(a) == covered.end()) chosen.push_back(a);
        }
        for (const auto &a : chosen) {
            covered.insert(a);
            if (use_partial) {
                auto it = atom_to_groups.find(a);
                if (it != atom_to_groups.end())
                    for (int g : it->second) --remaining[g];
            } else {
                --remaining[best];
            }
        }
        result.push_back(std::move(chosen));
    }

    // Singletons for remaining uncovered atoms.
    std::vector<ConditionPtr> uncovered;
    uncovered.reserve(atoms.size());
    for (const auto &a : atoms)
        if (covered.find(a) == covered.end()) uncovered.push_back(a);
    std::cout << uncovered.size() << " uncovered facts" << std::endl;
    std::sort(uncovered.begin(), uncovered.end(), atom_less);
    for (const auto &a : uncovered) result.push_back({a});
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
    utils::Timer t;
    auto raw = invariants::get_groups(task, reachable_action_parameters);
    std::cout << "    [fg.get_groups] " << t.seconds() << "s" << std::endl;
    t.reset();
    auto instantiated = instantiate_groups(raw, task, atoms);
    std::cout << "    [fg.instantiate_groups] " << t.seconds() << "s ("
              << raw.size() << " groups)" << std::endl;
    t.reset();
    auto sorted = sort_groups(std::move(instantiated));
    std::cout << "    [fg.sort_groups1] " << t.seconds() << "s" << std::endl;
    t.reset();
    ComputedGroups out;
    out.mutex_groups = collect_all_mutex_groups(sorted, atoms);
    std::cout << "    [fg.collect_mutex_groups] " << t.seconds() << "s "
              << "(" << out.mutex_groups.size() << " groups)" << std::endl;
    t.reset();
    auto chosen = choose_groups(sorted, atoms, negative_in_goal);
    std::cout << "    [fg.choose_groups] " << t.seconds() << "s "
              << "(" << chosen.size() << " groups)" << std::endl;
    t.reset();
    out.groups = sort_groups(std::move(chosen));
    std::cout << "    [fg.sort_groups2] " << t.seconds() << "s" << std::endl;
    t.reset();
    out.translation_key = build_translation_key(out.groups);
    std::cout << "    [fg.translation_key] " << t.seconds() << "s" << std::endl;
    return out;
}
}
