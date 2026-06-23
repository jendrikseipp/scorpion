#include "action.h"

using namespace std;
namespace translate::pddl {
namespace {
bool same_atom_and_args(const Literal &a, const Literal &b) {
    return a.predicate == b.predicate && a.args == b.args;
}

bool contains_add_effect_for(
    const vector<pair<vector<ConditionPtr>, ConditionPtr>>
        &add_effects,
    const vector<ConditionPtr> &cond,
    const Literal &positive) {
    for (const auto &[c, lit] : add_effects) {
        if (c.size() != cond.size() || !lit) continue;
        if (lit->kind() != Condition::Kind::ATOM) continue;
        const auto &eff_lit = static_cast<const Literal &>(*lit);
        if (!same_atom_and_args(eff_lit, positive)) continue;
        ConditionPtrEqual eq;
        bool conds_equal = true;
        for (size_t i = 0; i < c.size(); ++i)
            if (!eq(c[i], cond[i])) { conds_equal = false; break; }
        if (conds_equal) return true;
    }
    return false;
}
}

PropositionalAction::PropositionalAction(
    string name_, vector<ConditionPtr> precondition_,
    vector<pair<vector<ConditionPtr>, ConditionPtr>> effects,
    int cost_)
    : name(move(name_)),
      precondition(move(precondition_)),
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
        auto positive = make_shared<Atom>(literal.predicate, literal.args);
        if (!contains_add_effect_for(add_effects, cond, *positive))
            del_effects.emplace_back(cond, positive);
    }
}

Action::Action(string name_, vector<TypedObject> parameters_,
               int n, ConditionPtr precondition_,
               vector<Effect> effects_, shared_ptr<Increase> cost_)
    : name(move(name_)),
      parameters(move(parameters_)),
      num_external_parameters(n),
      precondition(move(precondition_)),
      effects(move(effects_)),
      cost(move(cost_)) {
    uniquify_variables();
}

void Action::uniquify_variables() {
    type_map.clear();
    for (const auto &p : parameters)
        type_map[p.name] = p.type_name;
    unordered_map<string, string> empty_renamings;
    if (precondition)
        precondition = precondition->uniquify_variables(type_map,
                                                        empty_renamings);
    for (auto &e : effects) {
        // Effect parameters (universal-effect bound vars) need fresh names.
        unordered_map<string, string> renamings;
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

void Action::dump(ostream &os) const {
    os << name << "(";
    for (size_t i = 0; i < parameters.size(); ++i) {
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

void PropositionalAction::dump(ostream &os) const {
    os << name << "\n";
    for (const auto &f : precondition) {
        os << "PRE: ";
        if (f) f->dump(os, 0);
    }
    auto dump_effects = [&os](const auto &effects, const char *label) {
        for (const auto &[cond, fact] : effects) {
            os << label << ": ";
            for (size_t i = 0; i < cond.size(); ++i) {
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
