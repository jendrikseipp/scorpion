#ifndef PDDL_PREDICATE_H
#define PDDL_PREDICATE_H

#include "types.h"

#include <ostream>
#include <string>
#include <vector>

namespace translate::pddl {
class Predicate {
public:
    std::string name;
    std::vector<TypedObject> arguments;

    Predicate() = default;
    Predicate(std::string name, std::vector<TypedObject> arguments)
        : name(std::move(name)), arguments(std::move(arguments)) {}

    int get_arity() const { return static_cast<int>(arguments.size()); }
};

std::ostream &operator<<(std::ostream &os, const Predicate &p);

/*
  PDDL functions (object fluents are not supported; type_name must be "number").
*/
class Function {
public:
    std::string name;
    std::vector<TypedObject> arguments;
    std::string type_name; // always "number"

    Function() = default;
    Function(std::string name, std::vector<TypedObject> arguments,
             std::string type_name)
        : name(std::move(name)), arguments(std::move(arguments)),
          type_name(std::move(type_name)) {}
};

std::ostream &operator<<(std::ostream &os, const Function &f);
}

#endif
