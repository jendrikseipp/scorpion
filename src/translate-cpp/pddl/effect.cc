#include "effect.h"

namespace translate::pddl {
namespace {
std::string pad(int indent) { return std::string(indent * 2, ' '); }
}

void SimpleEffect::dump(std::ostream &os, int indent) const {
    os << pad(indent) << "SimpleEffect\n";
    if (literal) literal->dump(os, indent + 1);
}

ConjunctiveEffect::ConjunctiveEffect(std::vector<AnyEffectPtr> effects)
    : effects() {
    for (auto &e : effects) {
        if (e && e->kind() == Kind::CONJUNCTIVE) {
            auto &c = static_cast<ConjunctiveEffect &>(*e);
            for (auto &child : c.effects)
                this->effects.push_back(std::move(child));
        } else {
            this->effects.push_back(std::move(e));
        }
    }
}

void ConjunctiveEffect::dump(std::ostream &os, int indent) const {
    os << pad(indent) << "ConjunctiveEffect\n";
    for (const auto &e : effects)
        if (e) e->dump(os, indent + 1);
}

ConditionalEffect::ConditionalEffect(ConditionPtr cond, AnyEffectPtr eff) {
    // If the child is itself conditional, fold the two conditions into one.
    if (eff && eff->kind() == Kind::CONDITIONAL) {
        auto &inner = static_cast<ConditionalEffect &>(*eff);
        std::vector<ConditionPtr> parts = {cond, inner.condition};
        condition = std::make_shared<Conjunction>(std::move(parts));
        effect = inner.effect;
    } else {
        condition = std::move(cond);
        effect = std::move(eff);
    }
}

void ConditionalEffect::dump(std::ostream &os, int indent) const {
    os << pad(indent) << "ConditionalEffect\n";
    os << pad(indent + 1) << "if\n";
    if (condition) condition->dump(os, indent + 2);
    os << pad(indent + 1) << "then\n";
    if (effect) effect->dump(os, indent + 2);
}

UniversalEffect::UniversalEffect(std::vector<TypedObject> params,
                                 AnyEffectPtr eff) {
    // Merge nested universal effects (matching the Python translator).
    if (eff && eff->kind() == Kind::UNIVERSAL) {
        auto &inner = static_cast<UniversalEffect &>(*eff);
        parameters = std::move(params);
        for (auto &p : inner.parameters)
            parameters.push_back(std::move(p));
        effect = inner.effect;
    } else {
        parameters = std::move(params);
        effect = std::move(eff);
    }
}

void UniversalEffect::dump(std::ostream &os, int indent) const {
    os << pad(indent) << "UniversalEffect (forall";
    for (std::size_t i = 0; i < parameters.size(); ++i)
        os << (i == 0 ? " " : ", ") << parameters[i];
    os << ")\n";
    if (effect) effect->dump(os, indent + 1);
}

void CostEffect::dump(std::ostream &os, int indent) const {
    os << pad(indent) << "CostEffect\n";
    if (effect) effect->dump(os, indent + 1);
}

void Effect::dump(std::ostream &os, int indent) const {
    std::string p = pad(indent);
    if (!parameters.empty()) {
        os << p << "forall";
        for (std::size_t i = 0; i < parameters.size(); ++i)
            os << (i == 0 ? " " : ", ") << parameters[i];
        os << "\n";
        p += "  ";
    }
    os << p << "if\n";
    if (condition) condition->dump(os, indent + (parameters.empty() ? 1 : 2));
    os << p << "then\n";
    if (literal) literal->dump(os, indent + (parameters.empty() ? 1 : 2));
}
}
