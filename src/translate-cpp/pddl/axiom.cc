#include "axiom.h"

namespace translate::pddl {
void Axiom::dump(std::ostream &os) const {
    os << "Axiom " << name << "(";
    for (int i = 0; i < num_external_parameters; ++i) {
        if (i) os << ", ";
        os << parameters[i];
    }
    os << ")\n";
    if (condition) condition->dump(os, 1);
}

void PropositionalAxiom::dump(std::ostream &os) const {
    os << name << "\n";
    for (const auto &lit : condition) {
        os << "PRE: ";
        if (lit) lit->dump(os, 0);
    }
    os << "EFF: ";
    if (effect) effect->dump(os, 0);
}
}
