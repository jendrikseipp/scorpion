#ifndef PDDL_EFFECT_H
#define PDDL_EFFECT_H

#include "condition.h"
#include "f_expression.h"

#include <memory>
#include <ostream>
#include <vector>

namespace translate::pddl {
/*
  Effects in the parsed PDDL. After normalization the unique form is
  `Effect`: a forall-conditional over a single literal. The other classes
  (ConditionalEffect, ConjunctiveEffect, UniversalEffect, SimpleEffect,
  CostEffect) represent the raw, un-normalized parse tree.
*/

class AnyEffect;
using AnyEffectPtr = std::shared_ptr<AnyEffect>;

class AnyEffect {
public:
    enum class Kind {
        SIMPLE, CONJUNCTIVE, CONDITIONAL, UNIVERSAL, COST
    };
    virtual ~AnyEffect() = default;
    virtual Kind kind() const = 0;
    virtual void dump(std::ostream &os, int indent = 0) const = 0;

    // Convert the raw parse tree to the canonical "outer Conjunctive, then
    // Universal, then Conditional, then Simple/Cost" form. Mirrors Python's
    // AnyEffect.normalize().
    virtual AnyEffectPtr normalize() const = 0;
};

class CostEffect;

class SimpleEffect final : public AnyEffect {
public:
    ConditionPtr literal; // Atom or NegatedAtom

    explicit SimpleEffect(ConditionPtr literal) : literal(std::move(literal)) {}
    Kind kind() const override { return Kind::SIMPLE; }
    void dump(std::ostream &os, int indent) const override;
    AnyEffectPtr normalize() const override;
};

class ConjunctiveEffect final : public AnyEffect {
public:
    std::vector<AnyEffectPtr> effects; // flattened on construction

    explicit ConjunctiveEffect(std::vector<AnyEffectPtr> effects);
    Kind kind() const override { return Kind::CONJUNCTIVE; }
    void dump(std::ostream &os, int indent) const override;
    AnyEffectPtr normalize() const override;
};

class ConditionalEffect final : public AnyEffect {
public:
    ConditionPtr condition;
    AnyEffectPtr effect;

    ConditionalEffect(ConditionPtr condition, AnyEffectPtr effect);
    Kind kind() const override { return Kind::CONDITIONAL; }
    void dump(std::ostream &os, int indent) const override;
    AnyEffectPtr normalize() const override;
};

class UniversalEffect final : public AnyEffect {
public:
    std::vector<TypedObject> parameters;
    AnyEffectPtr effect;

    UniversalEffect(std::vector<TypedObject> parameters, AnyEffectPtr effect);
    Kind kind() const override { return Kind::UNIVERSAL; }
    void dump(std::ostream &os, int indent) const override;
    AnyEffectPtr normalize() const override;
};

class CostEffect final : public AnyEffect {
public:
    std::shared_ptr<Increase> effect;

    explicit CostEffect(std::shared_ptr<Increase> effect)
        : effect(std::move(effect)) {}
    Kind kind() const override { return Kind::COST; }
    void dump(std::ostream &os, int indent) const override;
    AnyEffectPtr normalize() const override;
};

/*
  Separate out the CostEffect from a (post-normalize) effect tree.
  Returns (cost_effect, rest_effect). Either may be null.
*/
std::pair<std::shared_ptr<CostEffect>, AnyEffectPtr> extract_cost(
    const AnyEffectPtr &effect);

/*
  Normalized effect: forall `parameters`, if `condition` holds, apply
  `literal`. Mutable, matching the Python translator's Effect class.
*/
class Effect {
public:
    std::vector<TypedObject> parameters;
    ConditionPtr condition;
    ConditionPtr literal; // Atom or NegatedAtom

    Effect(std::vector<TypedObject> parameters, ConditionPtr condition,
           ConditionPtr literal)
        : parameters(std::move(parameters)),
          condition(std::move(condition)),
          literal(std::move(literal)) {}

    void dump(std::ostream &os, int indent = 0) const;
    bool equals(const Effect &other) const;
};
}

#endif
