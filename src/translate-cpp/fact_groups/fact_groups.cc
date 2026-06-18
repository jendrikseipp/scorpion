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
            if (reachable_facts.contains(fact)) result.push_back(fact);
        } else {
            for (const auto &obj : task.objects) {
                auto new_args = atom.args;
                new_args[pos] = obj.name;
                auto candidate = std::make_shared<const Atom>(
                    atom.predicate, std::move(new_args));
                if (reachable_facts.contains(candidate))
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
    for (auto &g : groups) std::ranges::sort(g, atom_less);
    std::ranges::sort(groups,
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
    std::ranges::sort(remaining, atom_less);
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
            if (!negative_in_goal.contains(a)) filtered.push_back(a);
        groups.push_back(std::move(filtered));
    }
    const int n = static_cast<int>(groups.size());
    const bool use_partial = get_options().use_partial_encoding;

    /*
      Faithful port of Python's GroupCoverQueue (fact_groups.py): greedily pick
      the largest remaining group and, under partial encoding, remove its facts
      from every other group (shrinking them). Reproducing Python's exact pop
      order is required for byte-identical selection on tasks with several
      equal-size candidate groups (e.g. freecell, where each card admits a
      "bottomcol ..." and a "clear ..." grouping of the same size).

      The order is LIFO within a size bucket over the input order, and a group
      shrunk by removal is lazily re-bucketed to the *back* of its new (smaller)
      bucket, so it is reconsidered before originally-smaller groups. We track
      live sizes with an int-counter array plus an atom->containing-groups index
      (rather than a hash set per group) to keep this O(sum of group sizes).
    */
    std::unordered_map<ConditionPtr, std::vector<int>,
                       ConditionPtrHash, ConditionPtrEqual> atom_to_groups;
    if (use_partial)
        for (int i = 0; i < n; ++i)
            for (const auto &a : groups[i]) atom_to_groups[a].push_back(i);

    std::vector<int> remaining(n);
    int max_size = 0;
    for (int i = 0; i < n; ++i) {
        remaining[i] = static_cast<int>(groups[i].size());
        max_size = std::max(max_size, remaining[i]);
    }
    std::vector<std::vector<int>> groups_by_size(max_size + 1);
    for (int i = 0; i < n; ++i)
        groups_by_size[remaining[i]].push_back(i);

    // Returns the next group to select (largest, LIFO, with lazy re-bucketing
    // of groups that shrank below their current bucket), or -1 when none with
    // more than one element remains.
    auto next_top = [&]() -> int {
        while (max_size > 1) {
            auto &bucket = groups_by_size[max_size];
            while (!bucket.empty()) {
                int cand = bucket.back();
                bucket.pop_back();
                if (remaining[cand] == max_size) return cand;
                groups_by_size[remaining[cand]].push_back(cand);
            }
            --max_size;
        }
        return -1;
    };

    AtomSet covered;
    std::vector<std::vector<ConditionPtr>> result;
    for (int top = next_top(); top >= 0; top = next_top()) {
        std::vector<ConditionPtr> chosen;
        if (use_partial) {
            // The live members of `top` are its still-uncovered atoms.
            for (const auto &a : groups[top])
                if (!covered.contains(a)) chosen.push_back(a);
            for (const auto &a : chosen) {
                covered.insert(a);
                for (int g : atom_to_groups[a]) --remaining[g];
            }
        } else {
            chosen = groups[top];
        }
        result.push_back(std::move(chosen));
    }

    AtomSet uncovered = atoms;
    for (const auto &g : result)
        for (const auto &a : g) uncovered.erase(a);
    std::vector<ConditionPtr> singles(uncovered.begin(), uncovered.end());
    std::cout << singles.size() << " uncovered facts" << std::endl;
    std::ranges::sort(singles, atom_less);
    for (const auto &a : singles) result.push_back({a});
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
