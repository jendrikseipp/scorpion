#ifndef TRANSLATE_PDDL_CONDITION_H
#define TRANSLATE_PDDL_CONDITION_H

#include "types.h"

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
    // collapse single-element junctions). Mirrors Python's Condition.simplified.
    virtual ConditionPtr simplified() const = 0;

    // Return a clone of this condition with its children replaced by
    // `new_parts`. Constants and literals ignore `new_parts` and return a
    // copy of themselves; junctors rebuild with the new children; quantifiers
    // keep their parameter list and replace the body.
    virtual ConditionPtr change_parts(
        std::vector<ConditionPtr> new_parts) const = 0;

    /*
      Make all quantifier-bound variable names globally unique. `type_map`
      accumulates (name -> type_name) bindings; `renamings` is a fresh map
      per quantifier scope (copied across nested quantifiers).
    */
    virtual ConditionPtr uniquify_variables(
        std::unordered_map<std::string, std::string> &type_map,
        const std::unordered_map<std::string, std::string> &renamings) const;
};

struct ConditionPtrHash {
    std::size_t operator()(const ConditionPtr &c) const noexcept {
        return c ? c->hash() : 0;
    }
};

struct ConditionPtrEqual {
    bool operator()(const ConditionPtr &a, const ConditionPtr &b) const {
        if (a.get() == b.get()) return true;
        if (!a || !b) return false;
        if (a->kind() != b->kind()) return false;
        return a->equals(*b);
    }
};

class Truth final : public Condition {
public:
    Kind kind() const override { return Kind::TRUTH; }
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
};

class Falsity final : public Condition {
public:
    Kind kind() const override { return Kind::FALSITY; }
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
};

/*
  Used during instantiation to abort processing of an action/axiom whose
  precondition is provably false in the given context.
*/
class Impossible : public std::exception {
public:
    const char *what() const noexcept override { return "Impossible"; }
};

class Literal : public Condition {
public:
    std::string predicate;
    std::vector<std::string> args;

protected:
    std::size_t cached_hash;
    Literal(std::string predicate, std::vector<std::string> args);

public:
    std::size_t hash() const override { return cached_hash; }
    void dump(std::ostream &os, int indent) const override;
    std::unordered_set<std::string> free_variables() const override;
    virtual bool negated() const = 0;
    ConditionPtr simplified() const override; // identity (literals don't simplify)
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
        : Literal(std::move(predicate), std::move(args)) {}
    Kind kind() const override { return Kind::ATOM; }
    bool equals(const Condition &other) const override;
    ConditionPtr negate() const override;
    bool negated() const override { return false; }
};

class NegatedAtom final : public Literal {
public:
    NegatedAtom(std::string predicate, std::vector<std::string> args)
        : Literal(std::move(predicate), std::move(args)) {}
    Kind kind() const override { return Kind::NEGATED_ATOM; }
    bool equals(const Condition &other) const override;
    ConditionPtr negate() const override;
    bool negated() const override { return true; }
};

class JunctorCondition : public Condition {
public:
    std::vector<ConditionPtr> children;
    std::size_t cached_hash;

protected:
    JunctorCondition(std::vector<ConditionPtr> children, Kind k);

public:
    const std::vector<ConditionPtr> &parts() const override { return children; }
    std::size_t hash() const override { return cached_hash; }
    bool equals(const Condition &other) const override;
    void dump(std::ostream &os, int indent) const override;
};

class Conjunction final : public JunctorCondition {
public:
    explicit Conjunction(std::vector<ConditionPtr> children)
        : JunctorCondition(std::move(children), Kind::CONJUNCTION) {}
    Kind kind() const override { return Kind::CONJUNCTION; }
    ConditionPtr negate() const override;
    ConditionPtr simplified() const override;
    ConditionPtr change_parts(std::vector<ConditionPtr> new_parts)
        const override {
        return std::make_shared<Conjunction>(std::move(new_parts));
    }
};

class Disjunction final : public JunctorCondition {
public:
    explicit Disjunction(std::vector<ConditionPtr> children)
        : JunctorCondition(std::move(children), Kind::DISJUNCTION) {}
    Kind kind() const override { return Kind::DISJUNCTION; }
    ConditionPtr negate() const override;
    bool has_disjunction() const override { return true; }
    ConditionPtr simplified() const override;
    ConditionPtr change_parts(std::vector<ConditionPtr> new_parts)
        const override {
        return std::make_shared<Disjunction>(std::move(new_parts));
    }
};

class QuantifiedCondition : public Condition {
public:
    std::vector<TypedObject> parameters;
    std::vector<ConditionPtr> body; // always size 1 (per Python)
    std::size_t cached_hash;

protected:
    QuantifiedCondition(std::vector<TypedObject> parameters,
                        std::vector<ConditionPtr> body, Kind k);

public:
    const std::vector<ConditionPtr> &parts() const override { return body; }
    std::size_t hash() const override { return cached_hash; }
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
    UniversalCondition(std::vector<TypedObject> parameters,
                       std::vector<ConditionPtr> body)
        : QuantifiedCondition(std::move(parameters), std::move(body),
                              Kind::UNIVERSAL) {}
    Kind kind() const override { return Kind::UNIVERSAL; }
    ConditionPtr negate() const override;
    bool has_universal_part() const override { return true; }
    ConditionPtr change_parts(std::vector<ConditionPtr> new_parts)
        const override {
        return std::make_shared<UniversalCondition>(parameters,
                                                    std::move(new_parts));
    }
};

class ExistentialCondition final : public QuantifiedCondition {
public:
    ExistentialCondition(std::vector<TypedObject> parameters,
                         std::vector<ConditionPtr> body)
        : QuantifiedCondition(std::move(parameters), std::move(body),
                              Kind::EXISTENTIAL) {}
    Kind kind() const override { return Kind::EXISTENTIAL; }
    ConditionPtr negate() const override;
    bool has_existential_part() const override { return true; }
    ConditionPtr change_parts(std::vector<ConditionPtr> new_parts)
        const override {
        return std::make_shared<ExistentialCondition>(parameters,
                                                      std::move(new_parts));
    }
};

// Convenience factories.
inline ConditionPtr make_truth() { return std::make_shared<Truth>(); }
inline ConditionPtr make_falsity() { return std::make_shared<Falsity>(); }
inline ConditionPtr make_atom(std::string predicate,
                              std::vector<std::string> args) {
    return std::make_shared<Atom>(std::move(predicate), std::move(args));
}
inline ConditionPtr make_negated_atom(std::string predicate,
                                      std::vector<std::string> args) {
    return std::make_shared<NegatedAtom>(std::move(predicate), std::move(args));
}
}

#endif
