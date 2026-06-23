#ifndef PDDL_TYPES_H
#define PDDL_TYPES_H

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace translate::pddl {
/*
  PDDL allows mixing types and predicates, but some PDDL files have name
  collisions between types and predicates. To support both, types are
  internally given predicate names "type@T" that cannot be confused with
  non-type predicates. This matches the Python translator's behavior.
*/
std::string type_predicate_name(std::string_view type_name);

class Type {
public:
    std::string name;
    std::optional<std::string> basetype_name;
    // Populated by set_supertypes() after the full type list has been parsed.
    std::vector<std::string> supertype_names;

    Type(std::string name, std::optional<std::string> basetype_name = {})
        : name(std::move(name)), basetype_name(std::move(basetype_name)) {}

    std::string get_predicate_name() const {
        return type_predicate_name(name);
    }
};

std::ostream &operator<<(std::ostream &os, const Type &t);

class TypedObject {
public:
    std::string name;
    std::string type_name;

    TypedObject() = default;
    TypedObject(std::string name, std::string type_name)
        : name(std::move(name)), type_name(std::move(type_name)) {}

    bool operator==(const TypedObject &other) const {
        return name == other.name && type_name == other.type_name;
    }

    bool operator!=(const TypedObject &other) const {
        return !(*this == other);
    }
};

std::ostream &operator<<(std::ostream &os, const TypedObject &o);

// Generate a fresh name not already in type_map; record mapping in renamings.
// Mirrors Python's TypedObject.uniquify_name.
TypedObject uniquify_name(
    const TypedObject &obj,
    std::unordered_map<std::string, std::string> &type_map,
    std::unordered_map<std::string, std::string> &renamings);
}

#endif
