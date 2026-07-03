#ifndef PDDL_CONDITION_H
#define PDDL_CONDITION_H

#include "types.h"

#include "algorithms/small_vector.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace translate::pddl {
/*
  Conditions are immutable, hashable, and shared (matching the Python
  translator). Use std::shared_ptr<const Condition> to refer to a node.

  Compound conditions (Conjunction, Disjunction, quantifiers) hold their
  child conditions in `parts`. Literals (Atom, NegatedAtom) hold predicate
  and string-typed arguments. Truth/Falsity are constants.

  Algorithmic transforms (simplify, normalize, untyped, instantiate, ...)
  live in dedicated modules (normalize.cc, instantiate.cc, ...). Only
  structural, context-free operations are placed here as virtual methods.
*/

class Condition;
using ConditionPtr = std::shared_ptr<const Condition>;

struct ConditionPtrHash;
struct ConditionPtrEqual;

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

// Marks a fact that is static and true in the initial state (present in init
// but not a reachable fluent). Distinguished from a fluent fact's FactId (>= 0)
// and from a fact absent from the map (unreachable / static-false).
inline constexpr FactId STATIC_FACT = -1;

// One map for all ground-fact membership tests during instantiation, so each
// literal is classified with a single probe: a reachable fluent fact maps to
// its dense FactId (>= 0); a static-true init fact maps to STATIC_FACT;
// anything absent is unreachable / static-false.
using FactMap = std::unordered_map<GroundKey, FactId, GroundKeyHash>;
// Kept name for the fluent-fact table exposed to the translator.
using FluentFactMap = FactMap;

class Condition {
public:
    enum class Kind {
        TRUTH,
        FALSITY,
        ATOM,
        NEGATED_ATOM,
        CONJUNCTION,
        DISJUNCTION,
        UNIVERSAL,
        EXISTENTIAL
    };

    virtual ~Condition() = default;

    virtual Kind kind() const = 0;
    virtual std::size_t hash() const = 0;
    virtual bool equals(const Condition &other) const = 0;
    virtual void dump(std::ostream &os, int indent = 0) const = 0;

    virtual const std::vector<ConditionPtr> &parts() const {
        static const std::vector<ConditionPtr> empty;
        return empty;
    }

    virtual ConditionPtr negate() const = 0;

    // ?-prefixed argument names (PDDL variable convention).
    virtual std::unordered_set<std::string> free_variables() const;
    virtual bool has_disjunction() const;
    virtual bool has_existential_part() const;
    virtual bool has_universal_part() const;

    // Returns a structurally simplified equivalent condition (flatten nested
    // junctors, drop Truth from conjunctions / Falsity from disjunctions,
    // collapse single-element junctions). Mirrors Python's
    // Condition.simplified.
    virtual ConditionPtr simplified() const = 0;

    // Return a clone of this condition with its children replaced by
    // `new_parts`. Constants and literals ignore `new_parts` and return a
    // copy of themselves; junctors rebuild with the new children; quantifiers
    // keep their parameter list and replace the body.
    virtual ConditionPtr change_parts(
        std::vector<ConditionPtr> new_parts) const = 0;

    /*
      Instantiate this (normalized) condition under `var_mapping`,
      appending ground literals (positive Atom or NegatedAtom) to
      `result`. Returns false if the condition is provably false in this
      context (the caller then drops the action/axiom); true otherwise.

      Default implementation throws std::runtime_error: only Truth,
      Falsity, Conjunction, ExistentialCondition, Atom and NegatedAtom
      can appear in normalized conditions, and each overrides this.
    */
    virtual bool instantiate(
        const VarMapping &var_mapping, const FluentFactMap &fluent_facts,
        std::vector<GroundLiteral> &result) const;

    /*
      Make all quantifier-bound variable names globally unique. `type_map`
      accumulates (name -> type_name) bindings; `renamings` is a fresh map
      per quantifier scope (copied across nested quantifiers).
    */
    virtual ConditionPtr uniquify_variables(
        std::unordered_map<std::string, std::string> &type_map,
        const std::unordered_map<std::string, std::string> &renamings) const;
};

namespace detail {
// boost::hash_combine-style mixing, shared by all condition hashers.
inline void hash_combine(std::size_t &seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
// Hash recipe for a (positive or negated) literal, used for Literal's
// cached_hash.
inline std::size_t literal_hash(
    const std::string &predicate,
    const std::vector<std::string> &args) noexcept {
    std::size_t h = std::hash<std::string>{}(predicate);
    for (const auto &a : args)
        hash_combine(h, std::hash<std::string>{}(a));
    return h;
}
}

struct ConditionPtrHash {
    std::size_t operator()(const ConditionPtr &c) const noexcept {
        return c ? c->hash() : 0;
    }
};

struct ConditionPtrEqual {
    bool operator()(const ConditionPtr &a, const ConditionPtr &b) const {
        if (a.get() == b.get())
            return true;
        if (!a || !b)
            return false;
        if (a->kind() != b->kind())
            return false;
        return a->equals(*b);
    }
};

// Set of ground atoms (the Result's fluent facts; used by fact_groups).
using AtomSet =
    std::unordered_set<ConditionPtr, ConditionPtrHash, ConditionPtrEqual>;

class Truth final : public Condition {
public:
    Kind kind() const override {
        return Kind::TRUTH;
    }
    std::size_t hash() const override;
    bool equals(const Condition &other) const override {
        return other.kind() == Kind::TRUTH;
    }
    void dump(std::ostream &os, int indent) const override;
    ConditionPtr negate() const override;
    ConditionPtr simplified() const override;
    ConditionPtr change_parts(std::vector<ConditionPtr>) const override {
        return std::make_shared<Truth>();
    }
    bool instantiate(
        const VarMapping &, const FluentFactMap &,
        std::vector<GroundLiteral> &) const override {
        return true;
    }
};

class Falsity final : public Condition {
public:
    Kind kind() const override {
        return Kind::FALSITY;
    }
    std::size_t hash() const override;
    bool equals(const Condition &other) const override {
        return other.kind() == Kind::FALSITY;
    }
    void dump(std::ostream &os, int indent) const override;
    ConditionPtr negate() const override;
    ConditionPtr simplified() const override;
    ConditionPtr change_parts(std::vector<ConditionPtr>) const override {
        return std::make_shared<Falsity>();
    }
    bool instantiate(
        const VarMapping &, const FluentFactMap &,
        std::vector<GroundLiteral> &) const override;
};

class Literal : public Condition {
public:
    std::string predicate;
    std::vector<std::string> args;
    // Interned predicate id (grounding symbol table), cached at construction so
    // instantiation builds integer ground-fact keys without re-interning the
    // predicate name on every probe.
    int predicate_id;

protected:
    std::size_t cached_hash;
    Literal(std::string predicate, std::vector<std::string> args);

public:
    std::size_t hash() const override {
        return cached_hash;
    }
    void dump(std::ostream &os, int indent) const override;
    std::unordered_set<std::string> free_variables() const override;
    virtual bool negated() const = 0;
    ConditionPtr
    simplified() const override; // identity (literals don't simplify)
    ConditionPtr uniquify_variables(
        std::unordered_map<std::string, std::string> &type_map,
        const std::unordered_map<std::string, std::string> &renamings)
        const override;
    ConditionPtr change_parts(std::vector<ConditionPtr>) const override;

    // Return a new literal with each occurrence of an argument name replaced
    // according to `renamings`. Used by Action::uniquify_variables, etc.
    ConditionPtr rename_variables(
        const std::unordered_map<std::string, std::string> &renamings) const;
};

class Atom final : public Literal {
public:
    Atom(std::string predicate, std::vector<std::string> args)
        : Literal(std::move(predicate), std::move(args)) {
    }
    Kind kind() const override {
        return Kind::ATOM;
    }
    bool equals(const Condition &other) const override;
    ConditionPtr negate() const override;
    bool negated() const override {
        return false;
    }
    bool instantiate(
        const VarMapping &var_mapping, const FluentFactMap &fluent_facts,
        std::vector<GroundLiteral> &result) const override;
};

class NegatedAtom final : public Literal {
public:
    NegatedAtom(std::string predicate, std::vector<std::string> args)
        : Literal(std::move(predicate), std::move(args)) {
    }
    Kind kind() const override {
        return Kind::NEGATED_ATOM;
    }
    bool equals(const Condition &other) const override;
    ConditionPtr negate() const override;
    bool negated() const override {
        return true;
    }
    bool instantiate(
        const VarMapping &var_mapping, const FluentFactMap &fluent_facts,
        std::vector<GroundLiteral> &result) const override;
};

class JunctorCondition : public Condition {
public:
    std::vector<ConditionPtr> children;
    std::size_t cached_hash;

protected:
    JunctorCondition(std::vector<ConditionPtr> children, Kind k);

public:
    const std::vector<ConditionPtr> &parts() const override {
        return children;
    }
    std::size_t hash() const override {
        return cached_hash;
    }
    bool equals(const Condition &other) const override;
    void dump(std::ostream &os, int indent) const override;
};

class Conjunction final : public JunctorCondition {
public:
    explicit Conjunction(std::vector<ConditionPtr> children)
        : JunctorCondition(std::move(children), Kind::CONJUNCTION) {
    }
    Kind kind() const override {
        return Kind::CONJUNCTION;
    }
    ConditionPtr negate() const override;
    ConditionPtr simplified() const override;
    ConditionPtr change_parts(
        std::vector<ConditionPtr> new_parts) const override {
        return std::make_shared<Conjunction>(std::move(new_parts));
    }
    bool instantiate(
        const VarMapping &var_mapping, const FluentFactMap &fluent_facts,
        std::vector<GroundLiteral> &result) const override;
};

class Disjunction final : public JunctorCondition {
public:
    explicit Disjunction(std::vector<ConditionPtr> children)
        : JunctorCondition(std::move(children), Kind::DISJUNCTION) {
    }
    Kind kind() const override {
        return Kind::DISJUNCTION;
    }
    ConditionPtr negate() const override;
    bool has_disjunction() const override {
        return true;
    }
    ConditionPtr simplified() const override;
    ConditionPtr change_parts(
        std::vector<ConditionPtr> new_parts) const override {
        return std::make_shared<Disjunction>(std::move(new_parts));
    }
};

class QuantifiedCondition : public Condition {
public:
    std::vector<TypedObject> parameters;
    std::vector<ConditionPtr> body; // always size 1 (per Python)
    std::size_t cached_hash;

protected:
    QuantifiedCondition(
        std::vector<TypedObject> parameters, std::vector<ConditionPtr> body,
        Kind k);

public:
    const std::vector<ConditionPtr> &parts() const override {
        return body;
    }
    std::size_t hash() const override {
        return cached_hash;
    }
    bool equals(const Condition &other) const override;
    void dump(std::ostream &os, int indent) const override;
    std::unordered_set<std::string> free_variables() const override;
    ConditionPtr simplified() const override;
    ConditionPtr uniquify_variables(
        std::unordered_map<std::string, std::string> &type_map,
        const std::unordered_map<std::string, std::string> &renamings)
        const override;
};

class UniversalCondition final : public QuantifiedCondition {
public:
    UniversalCondition(
        std::vector<TypedObject> parameters, std::vector<ConditionPtr> body)
        : QuantifiedCondition(
              std::move(parameters), std::move(body), Kind::UNIVERSAL) {
    }
    Kind kind() const override {
        return Kind::UNIVERSAL;
    }
    ConditionPtr negate() const override;
    bool has_universal_part() const override {
        return true;
    }
    ConditionPtr change_parts(
        std::vector<ConditionPtr> new_parts) const override {
        return std::make_shared<UniversalCondition>(
            parameters, std::move(new_parts));
    }
};

class ExistentialCondition final : public QuantifiedCondition {
public:
    ExistentialCondition(
        std::vector<TypedObject> parameters, std::vector<ConditionPtr> body)
        : QuantifiedCondition(
              std::move(parameters), std::move(body), Kind::EXISTENTIAL) {
    }
    Kind kind() const override {
        return Kind::EXISTENTIAL;
    }
    ConditionPtr negate() const override;
    bool has_existential_part() const override {
        return true;
    }
    ConditionPtr change_parts(
        std::vector<ConditionPtr> new_parts) const override {
        return std::make_shared<ExistentialCondition>(
            parameters, std::move(new_parts));
    }
    bool instantiate(
        const VarMapping &var_mapping, const FluentFactMap &fluent_facts,
        std::vector<GroundLiteral> &result) const override;
};

// Convenience factories. Truth and Falsity are immutable, value-equal
// constants, so hand out shared singletons instead of allocating a fresh node
// on every call (they are minted throughout normalize/simplify).
inline ConditionPtr make_truth() {
    static const ConditionPtr instance = std::make_shared<Truth>();
    return instance;
}
inline ConditionPtr make_falsity() {
    static const ConditionPtr instance = std::make_shared<Falsity>();
    return instance;
}
inline ConditionPtr make_atom(
    std::string predicate, std::vector<std::string> args) {
    return std::make_shared<Atom>(std::move(predicate), std::move(args));
}
inline ConditionPtr make_negated_atom(
    std::string predicate, std::vector<std::string> args) {
    return std::make_shared<NegatedAtom>(std::move(predicate), std::move(args));
}
}

#endif
