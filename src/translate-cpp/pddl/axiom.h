#ifndef TRANSLATE_PDDL_AXIOM_H
#define TRANSLATE_PDDL_AXIOM_H

#include "condition.h"

#include <ostream>
#include <string>
#include <vector>

namespace translate::pddl {
class Axiom {
public:
    std::string name;
    std::vector<TypedObject> parameters;
    int num_external_parameters; // always equals arity of derived predicate
    ConditionPtr condition;

    Axiom() : num_external_parameters(0) {}
    Axiom(std::string name, std::vector<TypedObject> parameters,
          int num_external_parameters, ConditionPtr condition)
        : name(std::move(name)),
          parameters(std::move(parameters)),
          num_external_parameters(num_external_parameters),
          condition(std::move(condition)) {}

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
