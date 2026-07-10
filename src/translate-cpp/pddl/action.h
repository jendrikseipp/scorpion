#ifndef PDDL_ACTION_H
#define PDDL_ACTION_H

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

    Action() : num_external_parameters(0) {
    }
    Action(
        std::string name, std::vector<TypedObject> parameters,
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
    // Ground literals as (FactId, sign); see GroundLiteral in condition.h.
    std::vector<GroundLiteral> precondition;
    // Each effect: (condition list, literal).
    std::vector<GroundEffect> add_effects;
    std::vector<GroundEffect> del_effects;
    int cost;

    PropositionalAction(
        std::string name, std::vector<GroundLiteral> precondition,
        const std::vector<GroundEffect> &effects, int cost);

    void dump(std::ostream &os) const;
};
}

#endif
