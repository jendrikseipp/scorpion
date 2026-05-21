#ifndef TRANSLATE_PDDL_ACTION_H
#define TRANSLATE_PDDL_ACTION_H

#include "condition.h"
#include "effect.h"
#include "f_expression.h"

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
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

    // Populated by uniquify_variables(); maps each bound variable name (the
    // action's parameters and any quantifier-bound vars in its body) to its
    // PDDL type name. Used by normalize and the Datalog grounder.
    std::unordered_map<std::string, std::string> type_map;

    Action() : num_external_parameters(0) {}
    Action(std::string name, std::vector<TypedObject> parameters,
           int num_external_parameters, ConditionPtr precondition,
           std::vector<Effect> effects, std::shared_ptr<Increase> cost);

    // Build type_map from parameters and uniquify quantifier-bound variables
    // in the precondition and effects relative to it. Mirrors Python's
    // Action.uniquify_variables and is called from the constructor.
    void uniquify_variables();

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
