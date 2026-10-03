#include "effect.h"

using namespace std;
namespace translate::pddl {
namespace {
string pad(int indent) {
    return string(indent * 2, ' ');
}
}

void SimpleEffect::dump(ostream &os, int indent) const {
    os << pad(indent) << "SimpleEffect\n";
    if (literal)
        literal->dump(os, indent + 1);
}

ConjunctiveEffect::ConjunctiveEffect(vector<AnyEffectPtr> effects) {
    for (auto &e : effects) {
        if (e && e->kind() == Kind::CONJUNCTIVE) {
            auto &c = static_cast<ConjunctiveEffect &>(*e);
            for (auto &child : c.effects)
                this->effects.push_back(move(child));
        } else {
            this->effects.push_back(move(e));
        }
    }
}

void ConjunctiveEffect::dump(ostream &os, int indent) const {
    os << pad(indent) << "ConjunctiveEffect\n";
    for (const auto &e : effects)
        if (e)
            e->dump(os, indent + 1);
}

ConditionalEffect::ConditionalEffect(ConditionPtr cond, AnyEffectPtr eff) {
    // If the child is itself conditional, fold the two conditions into one.
    if (eff && eff->kind() == Kind::CONDITIONAL) {
        auto &inner = static_cast<ConditionalEffect &>(*eff);
        vector<ConditionPtr> parts = {cond, inner.condition};
        condition = make_shared<Conjunction>(move(parts));
        effect = inner.effect;
    } else {
        condition = move(cond);
        effect = move(eff);
    }
}

void ConditionalEffect::dump(ostream &os, int indent) const {
    os << pad(indent) << "ConditionalEffect\n";
    os << pad(indent + 1) << "if\n";
    if (condition)
        condition->dump(os, indent + 2);
    os << pad(indent + 1) << "then\n";
    if (effect)
        effect->dump(os, indent + 2);
}

UniversalEffect::UniversalEffect(vector<TypedObject> params, AnyEffectPtr eff) {
    // Merge nested universal effects (matching the Python translator).
    if (eff && eff->kind() == Kind::UNIVERSAL) {
        auto &inner = static_cast<UniversalEffect &>(*eff);
        parameters = move(params);
        for (auto &p : inner.parameters)
            parameters.push_back(move(p));
        effect = inner.effect;
    } else {
        parameters = move(params);
        effect = move(eff);
    }
}

void UniversalEffect::dump(ostream &os, int indent) const {
    os << pad(indent) << "UniversalEffect (forall";
    for (size_t i = 0; i < parameters.size(); ++i)
        os << (i == 0 ? " " : ", ") << parameters[i];
    os << ")\n";
    if (effect)
        effect->dump(os, indent + 1);
}

void CostEffect::dump(ostream &os, int indent) const {
    os << pad(indent) << "CostEffect\n";
    if (effect)
        effect->dump(os, indent + 1);
}

bool Effect::equals(const Effect &other) const {
    if (parameters.size() != other.parameters.size())
        return false;
    for (size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i] != other.parameters[i])
            return false;
    ConditionPtrEqual eq;
    return eq(condition, other.condition) && eq(literal, other.literal);
}

void Effect::dump(ostream &os, int indent) const {
    string p = pad(indent);
    if (!parameters.empty()) {
        os << p << "forall";
        for (size_t i = 0; i < parameters.size(); ++i)
            os << (i == 0 ? " " : ", ") << parameters[i];
        os << "\n";
        p += "  ";
    }
    os << p << "if\n";
    if (condition)
        condition->dump(os, indent + (parameters.empty() ? 1 : 2));
    os << p << "then\n";
    if (literal)
        literal->dump(os, indent + (parameters.empty() ? 1 : 2));
}
}
