#include "types.h"

namespace translate::pddl {
std::string type_predicate_name(std::string_view type_name) {
    std::string result = "type@";
    result.append(type_name);
    return result;
}

std::ostream &operator<<(std::ostream &os, const Type &t) {
    return os << t.name;
}

std::ostream &operator<<(std::ostream &os, const TypedObject &o) {
    return os << o.name << ": " << o.type_name;
}

TypedObject uniquify_name(
    const TypedObject &obj,
    std::unordered_map<std::string, std::string> &type_map,
    std::unordered_map<std::string, std::string> &renamings) {
    if (type_map.find(obj.name) == type_map.end()) {
        type_map.emplace(obj.name, obj.type_name);
        return obj;
    }
    for (int counter = 1;; ++counter) {
        std::string new_name = obj.name + std::to_string(counter);
        if (type_map.find(new_name) == type_map.end()) {
            renamings.emplace(obj.name, new_name);
            type_map.emplace(new_name, obj.type_name);
            return TypedObject(new_name, obj.type_name);
        }
    }
}
}
