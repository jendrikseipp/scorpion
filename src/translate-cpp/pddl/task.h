#ifndef PDDL_TASK_H
#define PDDL_TASK_H

#include "action.h"
#include "axiom.h"
#include "condition.h"
#include "f_expression.h"
#include "predicate.h"
#include "types.h"

#include <memory>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace translate::pddl {
inline const std::vector<std::string> REQUIREMENT_LABELS = {
    ":strips", ":adl", ":typing", ":negation", ":equality",
    ":negative-preconditions", ":disjunctive-preconditions",
    ":existential-preconditions", ":universal-preconditions",
    ":quantified-preconditions", ":conditional-effects",
    ":derived-predicates", ":action-costs",
};

class Requirements {
public:
    std::vector<std::string> requirements;

    Requirements() = default;
    explicit Requirements(std::vector<std::string> requirements);
};

std::ostream &operator<<(std::ostream &os, const Requirements &r);

// An init element is either a ground atom or a numeric function assignment.
using InitElement = std::variant<std::shared_ptr<const Atom>,
                                 std::shared_ptr<Assign>>;

class Task {
public:
    std::string domain_name;
    std::string task_name;
    Requirements requirements;
    std::vector<Type> types;
    std::vector<TypedObject> objects;
    std::vector<Predicate> predicates;
    std::vector<Function> functions;
    std::vector<InitElement> init;
    ConditionPtr goal;
    std::vector<Action> actions;
    std::vector<Axiom> axioms;
    bool use_min_cost_metric = false;

    int axiom_counter = 0;

    // Create a new derived predicate; appends to both predicates and axioms.
    Axiom *add_axiom(std::vector<TypedObject> parameters, ConditionPtr cond);

    void dump(std::ostream &os) const;
};
}

#endif
