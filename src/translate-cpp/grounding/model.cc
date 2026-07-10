#include "model.h"

#include <algorithm>
#include <array>
#include <climits>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
namespace translate::grounding {
namespace {
/*
  Helper: replace argument names in `effect` with their position index,
  and rewrite condition args accordingly. Variables not in the effect are
  left as-is (only allowed for projection rules).
*/
pair<Atom, vector<Atom>> variables_to_numbers(
    const Atom &effect, const vector<Atom> &conditions) {
    unordered_map<string, int> rename;
    ArgList new_eff_args = effect.args;
    for (size_t i = 0; i < effect.args.size(); ++i) {
        if (effect.args[i].is_symbol()) {
            const string &s = effect.args[i].name();
            if (!s.empty() && s.front() == '?') {
                rename[s] = static_cast<int>(i);
                new_eff_args[i] = Arg(static_cast<int>(i));
            }
        }
    }
    Atom new_effect(effect.predicate, move(new_eff_args));
    vector<Atom> new_conds;
    new_conds.reserve(conditions.size());
    for (const auto &c : conditions) {
        ArgList new_args;
        new_args.reserve(c.args.size());
        for (const auto &a : c.args) {
            if (a.is_symbol()) {
                auto it = rename.find(a.name());
                if (it != rename.end()) {
                    new_args.emplace_back(it->second);
                    continue;
                }
            }
            new_args.push_back(a);
        }
        new_conds.emplace_back(c.predicate, move(new_args));
    }
    return {new_effect, move(new_conds)};
}

class BuildRule {
public:
    Atom effect;
    vector<Atom> conditions;
    virtual ~BuildRule() = default;
    BuildRule(Atom e, vector<Atom> c) : effect(move(e)), conditions(move(c)) {
    }
    /*
      `atom_index` is the position of `new_atom` in the model's `items`
      vector. Join/product rules store that index (4 bytes) rather than
      a copy of the atom's args, and look the args back up via `items`
      in fire(). This keeps the per-rule join indexes from duplicating
      every atom's args -- the dominant remaining memory cost on huge
      groundings (rovers-large-simple: the join index held ~9.7M args
      copies, ~2.7 GB).
    */
    /*
      Register `new_atom` (matching condition `cond_index`, stored at
      `atom_index` in the model) in this rule's join index, then emit every
      atom it produces by joining against the already-seen atoms. Index-then-
      emit is a single step so a join rule computes the atom's key once (it
      serves both the insert and the lookup) instead of once per phase.

      `enqueue` takes args by rvalue so each emitted atom can be moved into the
      queue's seen-set construction instead of copied. `items` is the model
      vector, used to dereference stored atom indices.
    */
    virtual void process(
        const Atom &new_atom, int atom_index, int cond_index,
        const vector<Atom> &items,
        const function<void(int, ArgList &&)> &enqueue) = 0;

protected:
    // Compute effect args using one new condition match.
    ArgList prepare_effect(const Atom &new_atom, int cond_index) {
        ArgList eff_args = effect.args;
        const auto &cond = conditions[cond_index];
        for (size_t i = 0; i < cond.args.size(); ++i) {
            if (cond.args[i].is_position())
                eff_args[cond.args[i].position()] = new_atom.args[i];
        }
        return eff_args;
    }
};

class ProjectRuleB : public BuildRule {
public:
    using BuildRule::BuildRule;
    void process(
        const Atom &new_atom, int, int cond_index, const vector<Atom> &,
        const function<void(int, ArgList &&)> &enqueue) override {
        auto eff_args = prepare_effect(new_atom, cond_index);
        enqueue(effect.predicate, move(eff_args));
    }
};

// Join key: the interned arg ids at the common positions. Args are already
// interned ints (Arg::v), so the key is a small int tuple with no per-firing
// string building. Almost always <= 2 common variables, so it stays inline.
using JoinKey = small_vector::SmallVector<int, 2>;
struct JoinKeyHash {
    size_t operator()(const JoinKey &k) const noexcept {
        size_t h = 1469598103934665603ULL; // FNV-1a
        for (size_t i = 0; i < k.size(); ++i) {
            h ^= static_cast<size_t>(static_cast<unsigned>(k[i]));
            h *= 1099511628211ULL;
        }
        return h;
    }
};

class JoinRuleB : public BuildRule {
public:
    // Positions of common variable args in each of the two conditions.
    array<vector<int>, 2> common_positions;
    // For each side: key (interned arg ids at the common positions) -> list of
    // indices into the model's `items` vector. We dereference items[idx].args
    // in fire(); storing indices instead of args copies keeps the join index
    // from duplicating every atom's args.
    array<unordered_map<JoinKey, vector<int>, JoinKeyHash>, 2> atoms_by_key;

    JoinRuleB(Atom e, vector<Atom> c) : BuildRule(move(e), move(c)) {
        const auto &la = conditions[0].args;
        const auto &ra = conditions[1].args;
        vector<int> lvars, rvars;
        for (const auto &a : la)
            if (a.is_position())
                lvars.push_back(a.position());
        for (const auto &a : ra)
            if (a.is_position())
                rvars.push_back(a.position());
        ranges::sort(lvars);
        ranges::sort(rvars);
        vector<int> common;
        set_intersection(
            lvars.begin(), lvars.end(), rvars.begin(), rvars.end(),
            back_inserter(common));
        for (int side = 0; side < 2; ++side) {
            const auto &args = conditions[side].args;
            for (int var : common) {
                for (size_t i = 0; i < args.size(); ++i) {
                    if (args[i].is_position() && args[i].position() == var) {
                        common_positions[side].push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
        }
    }

    static JoinKey key_of(const Atom &atom, const vector<int> &pos) {
        JoinKey k;
        k.reserve(pos.size());
        for (int p : pos)
            k.push_back(atom.args[p].v);
        return k;
    }

    void process(
        const Atom &new_atom, int atom_index, int cond_index,
        const vector<Atom> &items,
        const function<void(int, ArgList &&)> &enqueue) override {
        // The atom's key on this condition serves both to index it and to look
        // up matches on the other condition, so compute it once.
        JoinKey k = key_of(new_atom, common_positions[cond_index]);
        atoms_by_key[cond_index][k].push_back(atom_index);
        int other = 1 - cond_index;
        auto it = atoms_by_key[other].find(k);
        if (it == atoms_by_key[other].end())
            return;
        auto eff_args = prepare_effect(new_atom, cond_index);
        const auto &other_cond = conditions[other];
        for (int stored_idx : it->second) {
            const auto &stored_args = items[stored_idx].args;
            auto args = eff_args;
            for (size_t i = 0; i < other_cond.args.size(); ++i) {
                if (other_cond.args[i].is_position())
                    args[other_cond.args[i].position()] = stored_args[i];
            }
            enqueue(effect.predicate, move(args));
        }
    }
};

class ProductRuleB : public BuildRule {
public:
    // Per condition: indices into the model's `items` vector.
    vector<vector<int>> atoms_by_index;
    int empty_index_count;

    ProductRuleB(Atom e, vector<Atom> c)
        : BuildRule(move(e), move(c)),
          atoms_by_index(conditions.size()),
          empty_index_count(static_cast<int>(conditions.size())) {
    }

    void process(
        const Atom &new_atom, int atom_index, int cond_index,
        const vector<Atom> &items,
        const function<void(int, ArgList &&)> &enqueue) override {
        if (atoms_by_index[cond_index].empty())
            --empty_index_count;
        atoms_by_index[cond_index].push_back(atom_index);
        if (empty_index_count > 0)
            return;
        // Bindings from the new_atom for cond_index already applied via
        // prepare_effect. Recurse over the other conditions in ascending
        // order, skipping cond_index in place.
        ArgList eff_args = prepare_effect(new_atom, cond_index);
        recurse(0, cond_index, eff_args, items, enqueue);
    }

private:
    // Emit one atom per assignment of the not-yet-bound conditions (all except
    // cond_index) to already-seen atoms. A plain recursive member -- no
    // per-firing std::function or `positions` vector, both of which heap-
    // allocated in the model's hottest loop.
    void recurse(
        int p, int cond_index, const ArgList &args, const vector<Atom> &items,
        const function<void(int, ArgList &&)> &enqueue) const {
        if (p == cond_index)
            ++p;
        if (p >= static_cast<int>(conditions.size())) {
            ArgList copy = args;
            enqueue(effect.predicate, move(copy));
            return;
        }
        const auto &cond = conditions[p];
        for (int stored_idx : atoms_by_index[p]) {
            const auto &atom_args = items[stored_idx].args;
            ArgList next_args = args;
            for (size_t i = 0; i < cond.args.size(); ++i)
                if (cond.args[i].is_position())
                    next_args[cond.args[i].position()] = atom_args[i];
            recurse(p + 1, cond_index, next_args, items, enqueue);
        }
    }
};

vector<unique_ptr<BuildRule>> convert_rules(const Program &prog) {
    vector<unique_ptr<BuildRule>> result;
    result.reserve(prog.rules.size());
    for (const auto &r : prog.rules) {
        auto [eff, conds] = variables_to_numbers(r.effect, r.conditions);
        switch (r.kind) {
        case RuleKind::JOIN:
            result.push_back(make_unique<JoinRuleB>(eff, conds));
            break;
        case RuleKind::PRODUCT:
            result.push_back(make_unique<ProductRuleB>(eff, conds));
            break;
        case RuleKind::PROJECT:
            result.push_back(make_unique<ProjectRuleB>(eff, conds));
            break;
        default:
            throw runtime_error(
                "Rule has no kind set; was split_rules() called?");
        }
    }
    return result;
}

/*
  Unifier: maps predicate name to a set of (rule_index, cond_index) pairs
  whose condition pattern may match an incoming atom. The pattern is
  determined by the constant arguments at fixed positions. For
  simplicity we do not use a trie here — we test all candidates per
  predicate and check constant arguments. This is O(K * C) per atom (K =
  candidates, C = constants), which is acceptable; the original Python
  trie is an optimization we can revisit if benchmarks show it matters.
*/
struct CondRef {
    int rule_index;
    int cond_index;
    // (position, symbol id) pairs that must match the incoming atom.
    // Comparing interned ids avoids a string compare per constant in the
    // hot unify loop; equal names always intern to the same id.
    vector<pair<int, int>> constants;
};

class Unifier {
public:
    unordered_map<int, vector<CondRef>> by_predicate;

    void insert(int rule_index, int cond_index, const Atom &condition) {
        CondRef ref;
        ref.rule_index = rule_index;
        ref.cond_index = cond_index;
        for (size_t i = 0; i < condition.args.size(); ++i) {
            const Arg &a = condition.args[i];
            if (a.is_symbol()) {
                const string &s = a.name();
                if (!s.empty() && s.front() != '?')
                    ref.constants.emplace_back(static_cast<int>(i), a.v);
            }
        }
        by_predicate[condition.predicate].push_back(move(ref));
    }

    // Fill `out` with (rule_index, cond_index) matches for `atom`. Caller
    // owns `out` and clears it before each call; this lets the inner
    // vector storage be reused across the entire model build, avoiding
    // an allocation per atom.
    void unify(const Atom &atom, vector<pair<int, int>> &out) const {
        auto it = by_predicate.find(atom.predicate);
        if (it == by_predicate.end())
            return;
        for (const auto &ref : it->second) {
            bool ok = true;
            for (const auto &[p, v] : ref.constants) {
                if (p >= static_cast<int>(atom.args.size())) {
                    ok = false;
                    break;
                }
                const Arg &a = atom.args[p];
                if (!a.is_symbol() || a.v != v) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                out.emplace_back(ref.rule_index, ref.cond_index);
        }
    }
};

/*
  Queue of unique atoms (deduplicated via (predicate, args) hash).
*/
/*
  The semi-naive evaluator keeps every derived atom in `items` (this is
  the model we return) and needs a dedup set so each atom is enqueued at
  most once. Storing a *second* full Atom copy in the dedup set doubles
  peak memory, which is the dominant cost on huge groundings -- e.g.
  rovers-large-simple grounds to ~10M atoms and the duplicate storage
  pushed us past 8 GB while the Python translator fits in ~4 GB.

  Instead the dedup set stores 4-byte indices into `items`, with a hash
  and equality that dereference `items`. To test/insert a candidate we
  tentatively append it to `items`, try to insert its index, and roll
  back the append if an equal atom was already present. `items` only
  ever grows (pop just advances `pos`), so stored indices stay valid;
  reallocation moves the buffer but the index->Atom mapping is unchanged.
*/
/*
  Open-addressing set of atom indices, used to deduplicate derived atoms.
  It stores a single 4-byte index per slot in a flat table (with -1 marking an
  empty slot), rather than a node per element as std::unordered_set does. On the
  huge groundings this halves peak memory: the former pmr::unordered_set<int>
  cost ~24 bytes per element in nodes plus, because its monotonic arena never
  freed anything, every historical bucket array from each rehash (rovers-large:
  ~590 MB of the ~1.25 GB peak). Growth here reallocates one flat table and
  frees the old one.

  Hashing uses the caller's cached `hashes` vector (so growth never recomputes
  AtomHash); equality dereferences `items` on a hash collision.
*/
class IndexSet {
public:
    IndexSet(const vector<Atom> &items, const vector<uint32_t> &hashes)
        : items_(&items), hashes_(&hashes) {
    }
    // Insert index `idx` (its atom already appended to items/hashes). Returns
    // true if newly inserted, false if an equal atom was already present.
    bool insert(int idx) {
        if (cap_ == 0 || (count_ + 1) * 10 > cap_ * 7) // keep load factor < 0.7
            grow();
        size_t mask = cap_ - 1;
        size_t h = (*hashes_)[idx] & mask;
        while (slots_[h] != EMPTY) {
            int other = slots_[h];
            if ((*hashes_)[other] == (*hashes_)[idx] &&
                (*items_)[other] == (*items_)[idx])
                return false;
            h = (h + 1) & mask;
        }
        slots_[h] = idx;
        ++count_;
        return true;
    }

private:
    static constexpr int EMPTY = -1;
    const vector<Atom> *items_;
    const vector<uint32_t> *hashes_;
    vector<int> slots_;
    size_t cap_ = 0;
    size_t count_ = 0;

    void grow() {
        size_t new_cap = cap_ ? cap_ * 2 : (size_t{1} << 16);
        vector<int> new_slots(new_cap, EMPTY);
        size_t mask = new_cap - 1;
        for (int idx : slots_) {
            if (idx == EMPTY)
                continue;
            size_t h = (*hashes_)[idx] & mask;
            while (new_slots[h] != EMPTY)
                h = (h + 1) & mask;
            new_slots[h] = idx;
        }
        slots_.swap(new_slots);
        cap_ = new_cap;
    }
};

class AtomQueue {
public:
    vector<Atom> items;
    size_t pos = 0;
    size_t pushes = 0;

private:
    // Hash of items[i], cached so the dedup set never recomputes AtomHash
    // (which hashes the predicate string + args) when it rehashes on growth.
    // A splitmix64 finalizer is folded in so IndexSet can mask the low bits
    // directly: AtomHash leaves small/clustered low bits (e.g. 0-arity atoms
    // hash to their small predicate id), which would make open-addressing's
    // linear probing degrade catastrophically. Mixing is bijective, so it adds
    // no collisions.
    static size_t mix_hash(size_t x) noexcept {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }
    vector<uint32_t> hashes;
    IndexSet seen{items, hashes};

    // Append `a` to items, keep it only if not already seen.
    void insert_if_new(Atom &&a) {
        hashes.push_back(static_cast<uint32_t>(mix_hash(AtomHash{}(a))));
        items.push_back(move(a));
        int idx = static_cast<int>(items.size()) - 1;
        if (!seen.insert(idx)) {
            items.pop_back();
            hashes.pop_back();
        }
    }

public:
    explicit AtomQueue(vector<Atom> initial) {
        // Matches Python's `num_pushes = len(atoms)` initial count.
        pushes = initial.size();
        for (auto &a : initial)
            insert_if_new(move(a));
    }
    bool empty() const {
        return pos >= items.size();
    }
    void push(int pred, ArgList &&args) {
        ++pushes; // count every push attempt, like Python's queue.push
        insert_if_new(Atom(pred, move(args)));
    }
    Atom pop() {
        return items[pos++];
    }
};
}

vector<Atom> compute_model(const Program &prog) {
    cout << "Preparing model..." << endl;
    auto rules = convert_rules(prog);
    Unifier unifier;
    for (size_t i = 0; i < rules.size(); ++i) {
        for (size_t k = 0; k < rules[i]->conditions.size(); ++k) {
            unifier.insert(
                static_cast<int>(i), static_cast<int>(k),
                rules[i]->conditions[k]);
        }
    }
    vector<Atom> fact_atoms = prog.facts;
    sort(fact_atoms.begin(), fact_atoms.end());
    AtomQueue queue(move(fact_atoms));

    cout << "Generated " << rules.size() << " rules." << endl;
    cout << "Computing model..." << endl;
    // Precompute which predicate ids are auxiliary (name contains '$'), so the
    // hot pop-loop tests a flag by id instead of resolving each of the
    // (millions of) derived atoms' predicate names to a string and scanning
    // it. No predicates are interned during model computation, so the table
    // covers every predicate the loop can see.
    vector<char> is_auxiliary(symbols().size(), 0);
    for (size_t id = 0; id < is_auxiliary.size(); ++id)
        is_auxiliary[id] = symbols().name(id).find('$') != string::npos ? 1 : 0;
    size_t relevant = 0, auxiliary = 0;
    vector<pair<int, int>> matches;
    // Built once and reused: the callback only refers to `queue` (stable across
    // the loop), so there is no need to reconstruct a std::function for every
    // one of the millions of rule firings.
    const function<void(int, ArgList &&)> enqueue = [&](int p, ArgList &&args) {
        queue.push(p, move(args));
    };
    while (!queue.empty()) {
        // Index of the atom in queue.items, captured before pop advances.
        int idx = static_cast<int>(queue.pos);
        Atom next = queue.pop();
        if (is_auxiliary[next.predicate])
            ++auxiliary;
        else
            ++relevant;
        matches.clear();
        unifier.unify(next, matches);
        for (const auto &[ri, ci] : matches)
            rules[ri]->process(next, idx, ci, queue.items, enqueue);
    }
    cout << relevant << " relevant atoms" << endl;
    cout << auxiliary << " auxiliary atoms" << endl;
    cout << queue.items.size() << " final queue length" << endl;
    cout << queue.pushes << " total queue pushes" << endl;
    // Move (not copy) the model out: queue.items is a member of a local, so a
    // plain return would copy the whole ~500 MB atom vector on large groundings
    // -- a transient that both spikes peak RSS and wastes time.
    return move(queue.items);
}
}
