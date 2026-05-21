#ifndef TRANSLATE_PDDL_ACTION_H
#define TRANSLATE_PDDL_ACTION_H

#include "condition.h"
#include "effect.h"
#include "f_expression.h"

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace translate::pddl {
/*
  Schematic action. Parameters are typed. num_external_parameters is the
  number of leading parameters that appear in the grounded name; the
  remainder are "invisible" parameters introduced when compiling away
  existential quantifiers.
*/
class Action {
public:
    std::string name;
    std::vector<TypedObject> parameters;
    int num_external_parameters;
    ConditionPtr precondition;
    std::vector<Effect> effects;
    std::shared_ptr<Increase> cost; // optional

    Action() : num_external_parameters(0) {}
    Action(std::string name, std::vector<TypedObject> parameters,
           int num_external_parameters, ConditionPtr precondition,
           std::vector<Effect> effects, std::shared_ptr<Increase> cost)
        : name(std::move(name)),
          parameters(std::move(parameters)),
          num_external_parameters(num_external_parameters),
          precondition(std::move(precondition)),
          effects(std::move(effects)),
          cost(std::move(cost)) {}

    void dump(std::ostream &os) const;
};

class PropositionalAction {
public:
    std::string name;
    // Each precondition is an Atom or NegatedAtom.
    std::vector<ConditionPtr> precondition;
    // Each effect: (condition list, literal).
    std::vector<std::pair<std::vector<ConditionPtr>, ConditionPtr>> add_effects;
    std::vector<std::pair<std::vector<ConditionPtr>, ConditionPtr>> del_effects;
    int cost;

    PropositionalAction(std::string name,
                        std::vector<ConditionPtr> precondition,
                        std::vector<std::pair<std::vector<ConditionPtr>,
                                              ConditionPtr>> effects,
                        int cost);

    void dump(std::ostream &os) const;
};
}

#endif
