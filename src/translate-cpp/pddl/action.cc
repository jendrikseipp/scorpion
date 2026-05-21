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

Action::Action(std::string name_, std::vector<TypedObject> parameters_,
               int n, ConditionPtr precondition_,
               std::vector<Effect> effects_, std::shared_ptr<Increase> cost_)
    : name(std::move(name_)),
      parameters(std::move(parameters_)),
      num_external_parameters(n),
      precondition(std::move(precondition_)),
      effects(std::move(effects_)),
      cost(std::move(cost_)) {
    uniquify_variables();
}

void Action::uniquify_variables() {
    type_map.clear();
    for (const auto &p : parameters)
        type_map[p.name] = p.type_name;
    std::unordered_map<std::string, std::string> empty_renamings;
    if (precondition)
        precondition = precondition->uniquify_variables(type_map,
                                                        empty_renamings);
    for (auto &e : effects) {
        // Effect parameters (universal-effect bound vars) need fresh names.
        std::unordered_map<std::string, std::string> renamings;
        for (auto &par : e.parameters)
            par = pddl::uniquify_name(par, type_map, renamings);
        if (e.condition)
            e.condition = e.condition->uniquify_variables(type_map, renamings);
        if (e.literal) {
            const auto &lit = static_cast<const Literal &>(*e.literal);
            e.literal = lit.rename_variables(renamings);
        }
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
