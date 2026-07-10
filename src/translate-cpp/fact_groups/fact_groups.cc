#include "fact_groups.h"

#include "../translate_options.h"

#include "../grounding/symbols.h"
#include "../invariants/invariant_finder.h"

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using namespace std;
namespace translate::fact_groups {
using namespace pddl;

namespace {
int find_placeholder(const Atom &atom) {
    for (size_t i = 0; i < atom.args.size(); ++i)
        if (atom.args[i] == "?X")
            return static_cast<int>(i);
    return -1;
}

/*
  Integer key for a reachable atom (or a wildcard pattern): interned predicate
  id + interned object-id args, with WILDCARD in the one placeholder position.
  predicate_id is already cached on every Literal, and object names were
  interned during grounding, so building a key is a handful of plain intern()
  lookups.
*/
constexpr int WILDCARD = -1; // interned ids are >= 0

GroundKey atom_key(const Literal &lit) {
    GroundKey key;
    key.predicate = lit.predicate_id;
    key.args.reserve(lit.args.size());
    for (const auto &a : lit.args)
        key.args.push_back(grounding::symbols().intern(a));
    return key;
}

/*
  Reachability index for mutex-group expansion.

  A group candidate is a lifted atom that is either fully concrete or has one
  "?X" placeholder; expansion keeps every reachable ground atom that matches.
  Rather than allocate a fresh ground Atom and value-hash strings per candidate
  (the previous approach, and the fact-groups hot spot on object-heavy tasks
  like sokoban), we index the reachable atoms on interned-int keys:

    - `exact`: key -> stored atom, for concrete candidates (one probe each).
    - `by_wildcard`: for each reachable atom and each argument position, the key
      with that position blanked to WILDCARD -> the atoms sharing that pattern.
      A placeholder candidate then does a single lookup that returns *all* its
      matches at once, instead of looping over every object and probing each.
*/
struct ReachableIndex {
    unordered_map<GroundKey, ConditionPtr, GroundKeyHash> exact;
    unordered_map<GroundKey, vector<ConditionPtr>, GroundKeyHash> by_wildcard;
};

ReachableIndex build_reachable_index(const AtomSet &reachable_facts) {
    ReachableIndex index;
    index.exact.reserve(reachable_facts.size());
    for (const auto &f : reachable_facts) {
        if (!f || f->kind() != Condition::Kind::ATOM)
            continue;
        GroundKey key = atom_key(static_cast<const Literal &>(*f));
        for (size_t pos = 0; pos < key.args.size(); ++pos) {
            int obj = key.args[pos];
            key.args[pos] = WILDCARD;
            index.by_wildcard[key].push_back(f);
            key.args[pos] = obj;
        }
        index.exact.emplace(move(key), f);
    }
    return index;
}

vector<ConditionPtr> expand_group(
    const vector<ConditionPtr> &group, const ReachableIndex &reachable) {
    vector<ConditionPtr> result;
    for (const auto &fact : group) {
        if (!fact || fact->kind() != Condition::Kind::ATOM)
            continue;
        const auto &atom = static_cast<const Atom &>(*fact);
        int pos = find_placeholder(atom);
        GroundKey key = atom_key(atom);
        if (pos < 0) {
            auto it = reachable.exact.find(key);
            if (it != reachable.exact.end())
                result.push_back(it->second);
        } else {
            // All reachable atoms matching the pattern in one lookup. The
            // group's result is re-sorted by sort_groups, so their order here
            // (rather than the object order the old code walked) is equivalent.
            key.args[pos] = WILDCARD;
            auto it = reachable.by_wildcard.find(key);
            if (it != reachable.by_wildcard.end())
                result.insert(
                    result.end(), it->second.begin(), it->second.end());
        }
    }
    return result;
}

vector<vector<ConditionPtr>> instantiate_groups(
    const vector<vector<ConditionPtr>> &groups,
    const AtomSet &reachable_facts) {
    ReachableIndex reachable = build_reachable_index(reachable_facts);
    vector<vector<ConditionPtr>> result;
    result.reserve(groups.size());
    for (const auto &g : groups)
        result.push_back(expand_group(g, reachable));
    return result;
}

string atom_to_string(const ConditionPtr &c) {
    if (!c)
        return "";
    return static_cast<const Literal &>(*c).str();
}

bool atom_less(const ConditionPtr &a, const ConditionPtr &b) {
    if (!a || !b)
        return a.get() < b.get();
    const auto &la = static_cast<const Literal &>(*a);
    const auto &lb = static_cast<const Literal &>(*b);
    // Same predicate iff same cached id: use the int compare for the common
    // equal-predicate case, and only compare predicate *names* (for byte-
    // identical name ordering) when the predicates actually differ.
    if (la.predicate_id != lb.predicate_id)
        return la.predicate < lb.predicate;
    return la.args < lb.args;
}

vector<vector<ConditionPtr>> sort_groups(vector<vector<ConditionPtr>> groups) {
    for (auto &g : groups)
        ranges::sort(g, atom_less);
    ranges::sort(
        groups,
        [](const vector<ConditionPtr> &a, const vector<ConditionPtr> &b) {
            return lexicographical_compare(
                a.begin(), a.end(), b.begin(), b.end(), atom_less);
        });
    return groups;
}

// Dense id per reachable atom. The group/selection atoms are the very same
// shared_ptr<Condition> instances stored in `atoms`, so we key
// covered/uncovered state on an int id (via the atom's address) rather than
// value-hashing the shared_ptr in hash containers that are otherwise
// rebuilt/copied per call.
using AtomIds = unordered_map<const Condition *, int>;

vector<vector<ConditionPtr>> collect_all_mutex_groups(
    const vector<vector<ConditionPtr>> &groups, const AtomSet &atoms,
    const AtomIds &id_of) {
    vector<vector<ConditionPtr>> result;
    vector<char> in_group(id_of.size(), 0);
    for (const auto &g : groups) {
        for (const auto &a : g)
            in_group[id_of.at(a.get())] = 1;
        result.push_back(g);
    }
    vector<ConditionPtr> remaining;
    for (const auto &a : atoms)
        if (!in_group[id_of.at(a.get())])
            remaining.push_back(a);
    ranges::sort(remaining, atom_less);
    for (const auto &a : remaining)
        result.push_back({a});
    return result;
}

vector<vector<ConditionPtr>> choose_groups(
    const vector<vector<ConditionPtr>> &groups_in, const AtomSet &atoms,
    const AtomSet &negative_in_goal, const AtomIds &id_of) {
    // Optionally remove negative-in-goal atoms.
    vector<vector<ConditionPtr>> groups;
    groups.reserve(groups_in.size());
    for (const auto &g : groups_in) {
        vector<ConditionPtr> filtered;
        for (const auto &a : g)
            if (!negative_in_goal.contains(a))
                filtered.push_back(a);
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
    vector<vector<int>> atom_to_groups(id_of.size());
    if (use_partial)
        for (int i = 0; i < n; ++i)
            for (const auto &a : groups[i])
                atom_to_groups[id_of.at(a.get())].push_back(i);

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
                if (remaining[cand] == max_size)
                    return cand;
                groups_by_size[remaining[cand]].push_back(cand);
            }
            --max_size;
        }
        return -1;
    };

    vector<char> covered(id_of.size(), 0);
    vector<vector<ConditionPtr>> result;
    for (int top = next_top(); top >= 0; top = next_top()) {
        vector<ConditionPtr> chosen;
        if (use_partial) {
            // The live members of `top` are its still-uncovered atoms.
            for (const auto &a : groups[top])
                if (!covered[id_of.at(a.get())])
                    chosen.push_back(a);
            for (const auto &a : chosen) {
                const int id = id_of.at(a.get());
                covered[id] = 1;
                for (int g : atom_to_groups[id])
                    --remaining[g];
            }
        } else {
            chosen = groups[top];
        }
        result.push_back(move(chosen));
    }

    vector<char> in_result(id_of.size(), 0);
    for (const auto &g : result)
        for (const auto &a : g)
            in_result[id_of.at(a.get())] = 1;
    vector<ConditionPtr> singles;
    for (const auto &a : atoms)
        if (!in_result[id_of.at(a.get())])
            singles.push_back(a);
    cout << singles.size() << " uncovered facts" << endl;
    ranges::sort(singles, atom_less);
    for (const auto &a : singles)
        result.push_back({a});
    return result;
}

vector<vector<string>> build_translation_key(
    const vector<vector<ConditionPtr>> &groups) {
    vector<vector<string>> keys;
    keys.reserve(groups.size());
    for (const auto &g : groups) {
        vector<string> key;
        key.reserve(g.size());
        for (const auto &f : g)
            key.push_back(atom_to_string(f));
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
    const vector<vector<vector<int>>> *reachable_action_parameters,
    const AtomSet &negative_in_goal) {
    auto raw = invariants::get_groups(task, reachable_action_parameters);
    auto instantiated = instantiate_groups(raw, atoms);
    auto sorted = sort_groups(move(instantiated));
    ComputedGroups out;
    // Dense id per reachable atom (see AtomIds), assigned once and shared by
    // both selection passes below.
    AtomIds id_of;
    id_of.reserve(atoms.size());
    int next_id = 0;
    for (const auto &a : atoms)
        id_of.emplace(a.get(), next_id++);
    out.mutex_groups = collect_all_mutex_groups(sorted, atoms, id_of);
    auto chosen = choose_groups(sorted, atoms, negative_in_goal, id_of);
    out.groups = sort_groups(move(chosen));
    out.translation_key = build_translation_key(out.groups);
    return out;
}
}
