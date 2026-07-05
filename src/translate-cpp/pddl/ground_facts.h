#ifndef PDDL_GROUND_FACTS_H
#define PDDL_GROUND_FACTS_H

#include "algorithms/small_vector.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

/*
  Ground-fact runtime types used by the instantiation and STRIPS->SAS phases:
  the parameter binding (VarMapping), the integer ground-atom key (GroundKey),
  the dense fluent-fact id (FactId) and its membership table (FactMap), and the
  instantiated (conditional) effect literals (GroundLiteral, GroundEffect).
  These are separate from the lifted Condition AST in condition.h; condition.h
  includes this header because its instantiate() signatures refer to these
  types.
*/
namespace translate::pddl {
/*
  Binding from a parameter name (e.g. "?x") to an interned object id, used
  while instantiating a normalized condition. An action/axiom has only a handful
  of parameters, so a flat vector with linear lookup beats std::unordered_map:
  it holds one buffer instead of a node per entry (instantiate_action rebuilds
  the binding for every ground action, so the map's clear()+re-insert otherwise
  frees and re-allocates those nodes millions of times) and it avoids hashing
  the short "?x" keys. The value is the object's interned id (from the grounding
  symbol table) so literal instantiation can build integer ground-fact keys and
  probe init/fluent facts with int comparisons instead of string hashing. Only
  lookups matter; iteration order is irrelevant.
*/
class VarMapping {
public:
    using value_type = std::pair<std::string, int>;
    using const_iterator = std::vector<value_type>::const_iterator;

    const_iterator begin() const {
        return entries_.begin();
    }
    const_iterator end() const {
        return entries_.end();
    }
    const_iterator find(const std::string &key) const {
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->first == key)
                return it;
        return entries_.end();
    }
    // Insert-or-access, like std::unordered_map::operator[].
    int &operator[](const std::string &key) {
        for (auto &e : entries_)
            if (e.first == key)
                return e.second;
        entries_.emplace_back(key, 0);
        return entries_.back().second;
    }
    void clear() {
        entries_.clear();
    }

private:
    std::vector<value_type> entries_;
};

/*
  Integer key for a ground atom: interned predicate id + interned object-id
  args (ids from the grounding symbol table, shared with the Datalog model).
  Comparing/hashing ints replaces the string hashing + memcmp that dominated
  the instantiation phase when init/fluent facts were probed as string atoms.
*/
struct GroundKey {
    int predicate;
    small_vector::SmallVector<int, 4> args;
    bool operator==(const GroundKey &o) const {
        return predicate == o.predicate && args == o.args;
    }
};
struct GroundKeyHash {
    std::size_t operator()(const GroundKey &k) const noexcept {
        std::size_t h = std::hash<int>{}(k.predicate);
        for (int a : k.args)
            h ^= std::hash<int>{}(a) + 0x9e3779b97f4a7c15ULL + (h << 6) +
                 (h >> 2);
        return h;
    }
};
// A dense index over the reachable fluent facts (0 .. #fluent-1), assigned
// when the fluent-fact table is built.
using FactId = int;

/*
  An instantiated ground literal: a fluent fact (by dense FactId) plus a sign.
  Replaces shared_ptr<Atom> for the many instantiated action literals -- 8 bytes
  with no Atom object or strings, versus a 16-byte shared_ptr plus a
  string-bearing Atom. Downstream (STRIPS->SAS translation) only needs the
  fact's identity and the sign, both captured here.
*/
struct GroundLiteral {
    FactId fact;
    bool negated;
    GroundLiteral negate() const {
        return {fact, !negated};
    }
    bool operator==(const GroundLiteral &) const = default;
};

// One instantiated (conditional) effect: `literal` fires when every ground
// literal in `conditions` holds (an empty list is an unconditional effect).
// Replaces the anonymous pair<vector<GroundLiteral>, GroundLiteral>; member
// order matches the old pair so structured bindings and aggregate init still
// work.
struct GroundEffect {
    std::vector<GroundLiteral> conditions;
    GroundLiteral literal;
};

// Marks a fact that is static and true in the initial state (present in init
// but not a reachable fluent). Distinguished from a fluent fact's FactId (>= 0)
// and from a fact absent from the map (unreachable / static-false).
inline constexpr FactId STATIC_FACT = -1;

// One map for all ground-fact membership tests during instantiation, so each
// literal is classified with a single probe: a reachable fluent fact maps to
// its dense FactId (>= 0); a static-true init fact maps to STATIC_FACT;
// anything absent is unreachable / static-false.
//
// Built once, then probed once per instantiated literal (millions of times).
// A flat open-addressing table keeps each key/value inline, so a probe is a
// hash plus a contiguous scan of slots instead of a node-pointer chase; the
// splitmix finalizer spreads GroundKeyHash's low bits so linear probing stays
// short. `insert` is first-wins (like unordered_map::emplace): fluent facts
// are inserted before static-init facts and keep priority.
class FactMap {
public:
    void reserve(std::size_t n) {
        std::size_t cap = 16;
        while (cap * 7 < n * 10)
            cap <<= 1;
        if (cap > cap_)
            rehash(cap);
    }
    void insert(GroundKey key, FactId id) {
        if (cap_ == 0 || (count_ + 1) * 10 > cap_ * 7)
            rehash(cap_ ? cap_ * 2 : 16);
        std::size_t mask = cap_ - 1;
        std::size_t h = mix(GroundKeyHash{}(key)) & mask;
        while (slots_[h].id != EMPTY) {
            if (slots_[h].key == key)
                return; // first insert wins
            h = (h + 1) & mask;
        }
        slots_[h].key = std::move(key);
        slots_[h].id = id;
        ++count_;
    }
    // Pointer to the stored FactId for `key`, or nullptr if absent.
    const FactId *find(const GroundKey &key) const {
        if (cap_ == 0)
            return nullptr;
        std::size_t mask = cap_ - 1;
        std::size_t h = mix(GroundKeyHash{}(key)) & mask;
        while (slots_[h].id != EMPTY) {
            if (slots_[h].key == key)
                return &slots_[h].id;
            h = (h + 1) & mask;
        }
        return nullptr;
    }
    std::size_t size() const {
        return count_;
    }

private:
    static constexpr FactId EMPTY = -2; // valid ids are >= 0; STATIC_FACT is -1
    struct Slot {
        GroundKey key;
        FactId id = EMPTY;
    };
    std::vector<Slot> slots_;
    std::size_t cap_ = 0;
    std::size_t count_ = 0;

    static std::size_t mix(std::size_t x) noexcept {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }
    void rehash(std::size_t new_cap) {
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(new_cap, Slot{});
        cap_ = new_cap;
        count_ = 0;
        std::size_t mask = new_cap - 1;
        for (auto &s : old)
            if (s.id != EMPTY) {
                std::size_t h = mix(GroundKeyHash{}(s.key)) & mask;
                while (slots_[h].id != EMPTY)
                    h = (h + 1) & mask;
                slots_[h] = std::move(s);
                ++count_;
            }
    }
};
}

#endif
