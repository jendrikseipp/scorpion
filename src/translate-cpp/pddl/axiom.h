#ifndef TRANSLATE_PDDL_AXIOM_H
#define TRANSLATE_PDDL_AXIOM_H

#include "condition.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace translate::pddl {
class Axiom {
public:
    std::string name;
    std::vector<TypedObject> parameters;
    int num_external_parameters; // always equals arity of derived predicate
    ConditionPtr condition;

    // Populated by uniquify_variables(); like Action::type_map.
    std::unordered_map<std::string, std::string> type_map;

    Axiom() : num_external_parameters(0) {}
    Axiom(std::string name, std::vector<TypedObject> parameters,
          int num_external_parameters, ConditionPtr condition);

    void uniquify_variables();

    void dump(std::ostream &os) const;
};

class PropositionalAxiom {
public:
    std::string name;
    std::vector<ConditionPtr> condition; // list of literals
    std::shared_ptr<const Atom> effect;

    PropositionalAxiom(std::string name, std::vector<ConditionPtr> condition,
                       std::shared_ptr<const Atom> effect)
        : name(std::move(name)), condition(std::move(condition)),
          effect(std::move(effect)) {}

    void dump(std::ostream &os) const;
};
}

#endif
