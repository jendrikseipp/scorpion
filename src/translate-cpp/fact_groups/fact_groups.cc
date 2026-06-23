#include "fact_groups.h"

#include "../invariants/invariant_finder.h"
#include "../translate_options.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace std;
namespace translate::fact_groups {
using namespace pddl;

namespace {
int find_placeholder(const Atom &atom) {
    for (size_t i = 0; i < atom.args.size(); ++i)
        if (atom.args[i] == "?X") return static_cast<int>(i);
    return -1;
}

vector<ConditionPtr> expand_group(
    const vector<ConditionPtr> &group, const Task &task,
    const AtomSet &reachable_facts) {
    vector<ConditionPtr> result;
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
                auto candidate = make_shared<const Atom>(
                    atom.predicate, move(new_args));
                if (reachable_facts.contains(candidate))
                    result.push_back(candidate);
            }
        }
    }
    return result;
}

vector<vector<ConditionPtr>> instantiate_groups(
    const vector<vector<ConditionPtr>> &groups, const Task &task,
    const AtomSet &reachable_facts) {
    vector<vector<ConditionPtr>> result;
    result.reserve(groups.size());
    for (const auto &g : groups)
        result.push_back(expand_group(g, task, reachable_facts));
    return result;
}

string atom_to_string(const ConditionPtr &c) {
    if (!c) return "";
    const auto &lit = static_cast<const Literal &>(*c);
    ostringstream os;
    if (lit.negated()) os << "Negated";
    os << "Atom " << lit.predicate << "(";
    for (size_t i = 0; i < lit.args.size(); ++i) {
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

vector<vector<ConditionPtr>> sort_groups(
    vector<vector<ConditionPtr>> groups) {
    for (auto &g : groups) ranges::sort(g, atom_less);
    ranges::sort(groups,
              [](const vector<ConditionPtr> &a,
                 const vector<ConditionPtr> &b) {
                  return lexicographical_compare(
                      a.begin(), a.end(), b.begin(), b.end(), atom_less);
              });
    return groups;
}

vector<vector<ConditionPtr>> collect_all_mutex_groups(
    const vector<vector<ConditionPtr>> &groups,
    const AtomSet &atoms) {
    vector<vector<ConditionPtr>> result;
    AtomSet uncovered = atoms;
    for (const auto &g : groups) {
        for (const auto &a : g) uncovered.erase(a);
        result.push_back(g);
    }
    vector<ConditionPtr> remaining(uncovered.begin(), uncovered.end());
    ranges::sort(remaining, atom_less);
    for (const auto &a : remaining)
        result.push_back({a});
    return result;
}

vector<vector<ConditionPtr>> choose_groups(
    const vector<vector<ConditionPtr>> &groups_in,
    const AtomSet &atoms, const AtomSet &negative_in_goal) {
    // Optionally remove negative-in-goal atoms.
    vector<vector<ConditionPtr>> groups;
    groups.reserve(groups_in.size());
    for (const auto &g : groups_in) {
        vector<ConditionPtr> filtered;
        for (const auto &a : g)
            if (!negative_in_goal.contains(a)) filtered.push_back(a);
        groups.push_back(move(filtered));
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
    unordered_map<ConditionPtr, vector<int>,
                       ConditionPtrHash, ConditionPtrEqual> atom_to_groups;
    if (use_partial)
        for (int i = 0; i < n; ++i)
            for (const auto &a : groups[i]) atom_to_groups[a].push_back(i);

    vector<int> remaining(n);
    int max_size = 0;
    for (int i = 0; i < n; ++i) {
        remaining[i] = static_cast<int>(groups[i].size());
        max_size = max(max_size, remaining[i]);
    }
    vector<vector<int>> groups_by_size(max_size + 1);
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
    vector<vector<ConditionPtr>> result;
    for (int top = next_top(); top >= 0; top = next_top()) {
        vector<ConditionPtr> chosen;
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
        result.push_back(move(chosen));
    }

    AtomSet uncovered = atoms;
    for (const auto &g : result)
        for (const auto &a : g) uncovered.erase(a);
    vector<ConditionPtr> singles(uncovered.begin(), uncovered.end());
    cout << singles.size() << " uncovered facts" << endl;
    ranges::sort(singles, atom_less);
    for (const auto &a : singles) result.push_back({a});
    return result;
}

vector<vector<string>> build_translation_key(
    const vector<vector<ConditionPtr>> &groups) {
    vector<vector<string>> keys;
    keys.reserve(groups.size());
    for (const auto &g : groups) {
        vector<string> key;
        for (const auto &f : g) key.push_back(atom_to_string(f));
        if (g.size() == 1) {
            const auto &lit = static_cast<const Literal &>(*g[0]);
            auto neg = lit.negate();
            key.push_back(atom_to_string(neg));
        } else {
            key.push_back("<none of those>");
        }
        keys.push_back(move(key));
    }
    return keys;
}
}

ComputedGroups compute_groups(
    const Task &task, const AtomSet &atoms,
    const vector<vector<vector<string>>>
        *reachable_action_parameters,
    const AtomSet &negative_in_goal) {
    auto raw = invariants::get_groups(task, reachable_action_parameters);
    auto instantiated = instantiate_groups(raw, task, atoms);
    auto sorted = sort_groups(move(instantiated));
    ComputedGroups out;
    out.mutex_groups = collect_all_mutex_groups(sorted, atoms);
    auto chosen = choose_groups(sorted, atoms, negative_in_goal);
    out.groups = sort_groups(move(chosen));
    out.translation_key = build_translation_key(out.groups);
    return out;
}
}
