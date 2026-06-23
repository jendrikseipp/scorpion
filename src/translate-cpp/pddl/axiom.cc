#include "axiom.h"

using namespace std;
namespace translate::pddl {
Axiom::Axiom(string name_, vector<TypedObject> parameters_,
             int n, ConditionPtr condition_)
    : name(move(name_)),
      parameters(move(parameters_)),
      num_external_parameters(n),
      condition(move(condition_)) {
    uniquify_variables();
}

void Axiom::uniquify_variables() {
    type_map.clear();
    for (const auto &p : parameters)
        type_map[p.name] = p.type_name;
    unordered_map<string, string> empty_renamings;
    if (condition)
        condition = condition->uniquify_variables(type_map, empty_renamings);
}

void Axiom::dump(ostream &os) const {
    os << "Axiom " << name << "(";
    for (int i = 0; i < num_external_parameters; ++i) {
        if (i) os << ", ";
        os << parameters[i];
    }
    os << ")\n";
    if (condition) condition->dump(os, 1);
}

void PropositionalAxiom::dump(ostream &os) const {
    os << name << "\n";
    for (const auto &lit : condition) {
        os << "PRE: ";
        if (lit) lit->dump(os, 0);
    }
    os << "EFF: ";
    if (effect) effect->dump(os, 0);
}
}
