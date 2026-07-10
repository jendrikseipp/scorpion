#include "action.h"

using namespace std;
namespace translate::pddl {
namespace {
bool contains_add_effect_for(
    const vector<GroundEffect> &add_effects, const vector<GroundLiteral> &cond,
    FactId fact) {
    for (const auto &[c, lit] : add_effects)
        if (lit.fact == fact && c == cond)
            return true;
    return false;
}
}

PropositionalAction::PropositionalAction(
    string name_, vector<GroundLiteral> precondition_,
    const vector<GroundEffect> &effects, int cost_)
    : name(move(name_)), precondition(move(precondition_)), cost(cost_) {
    for (const auto &[cond, lit] : effects)
        if (!lit.negated)
            add_effects.emplace_back(cond, lit);
    // A negated effect deletes the fact: record it as the positive fact in
    // del_effects (dropping duplicates already covered by an equal add effect).
    for (const auto &[cond, lit] : effects) {
        if (!lit.negated)
            continue;
        if (!contains_add_effect_for(add_effects, cond, lit.fact))
            del_effects.emplace_back(cond, GroundLiteral{lit.fact, false});
    }
}

Action::Action(
    string name_, vector<TypedObject> parameters_, int n,
    ConditionPtr precondition_, vector<Effect> effects_,
    shared_ptr<Increase> cost_)
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
        precondition =
            precondition->uniquify_variables(type_map, empty_renamings);
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
        if (i)
            os << ", ";
        os << parameters[i];
    }
    os << ")\nPrecondition:\n";
    if (precondition)
        precondition->dump(os, 1);
    os << "Effects:\n";
    for (const auto &e : effects)
        e.dump(os, 1);
    os << "Cost:\n";
    if (cost)
        cost->dump(os, 1);
    else
        os << "  None\n";
}

void PropositionalAction::dump(ostream &os) const {
    auto lit = [&os](const GroundLiteral &l) {
        os << (l.negated ? "!" : "") << "fact" << l.fact;
    };
    os << name << "\n";
    for (const auto &l : precondition) {
        os << "PRE: ";
        lit(l);
        os << "\n";
    }
    auto dump_effects = [&](const auto &effects, const char *label) {
        for (const auto &[cond, fact] : effects) {
            os << label << ": ";
            for (size_t i = 0; i < cond.size(); ++i) {
                if (i)
                    os << ", ";
                lit(cond[i]);
            }
            os << " -> ";
            lit(fact);
            os << "\n";
        }
    };
    dump_effects(add_effects, "ADD");
    dump_effects(del_effects, "DEL");
    os << "cost: " << cost << "\n";
}
}
