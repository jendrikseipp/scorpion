#include "action.h"

namespace translate::pddl {
namespace {
bool same_atom_and_args(const Literal &a, const Literal &b) {
    return a.predicate == b.predicate && a.args == b.args;
}

bool contains_add_effect_for(
    const std::vector<std::pair<std::vector<ConditionPtr>, ConditionPtr>>
        &add_effects,
    const std::vector<ConditionPtr> &cond,
    const Literal &positive) {
    for (const auto &[c, lit] : add_effects) {
        if (c.size() != cond.size() || !lit) continue;
        if (lit->kind() != Condition::Kind::ATOM) continue;
        const auto &eff_lit = static_cast<const Literal &>(*lit);
        if (!same_atom_and_args(eff_lit, positive)) continue;
        ConditionPtrEqual eq;
        bool conds_equal = true;
        for (std::size_t i = 0; i < c.size(); ++i)
            if (!eq(c[i], cond[i])) { conds_equal = false; break; }
        if (conds_equal) return true;
    }
    return false;
}
}

PropositionalAction::PropositionalAction(
    std::string name_, std::vector<ConditionPtr> precondition_,
    std::vector<std::pair<std::vector<ConditionPtr>, ConditionPtr>> effects,
    int cost_)
    : name(std::move(name_)),
      precondition(std::move(precondition_)),
      cost(cost_) {
    for (const auto &[cond, lit] : effects) {
        if (!lit) continue;
        const auto &literal = static_cast<const Literal &>(*lit);
        if (!literal.negated())
            add_effects.emplace_back(cond, lit);
    }
    for (auto &[cond, lit] : effects) {
        if (!lit) continue;
        const auto &literal = static_cast<const Literal &>(*lit);
        if (!literal.negated()) continue;
        auto positive = std::make_shared<Atom>(literal.predicate, literal.args);
        if (!contains_add_effect_for(add_effects, cond, *positive))
            del_effects.emplace_back(cond, positive);
    }
}

void Action::dump(std::ostream &os) const {
    os << name << "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i) os << ", ";
        os << parameters[i];
    }
    os << ")\nPrecondition:\n";
    if (precondition) precondition->dump(os, 1);
    os << "Effects:\n";
    for (const auto &e : effects)
        e.dump(os, 1);
    os << "Cost:\n";
    if (cost) cost->dump(os, 1);
    else os << "  None\n";
}

void PropositionalAction::dump(std::ostream &os) const {
    os << name << "\n";
    for (const auto &f : precondition) {
        os << "PRE: ";
        if (f) f->dump(os, 0);
    }
    auto dump_effects = [&os](const auto &effects, const char *label) {
        for (const auto &[cond, fact] : effects) {
            os << label << ": ";
            for (std::size_t i = 0; i < cond.size(); ++i) {
                if (i) os << ", ";
                if (cond[i]) cond[i]->dump(os, 0);
            }
            os << " -> ";
            if (fact) fact->dump(os, 0);
        }
    };
    dump_effects(add_effects, "ADD");
    dump_effects(del_effects, "DEL");
    os << "cost: " << cost << "\n";
}
}
