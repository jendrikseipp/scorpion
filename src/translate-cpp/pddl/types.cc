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

namespace {
bool contains_name(
    const std::vector<std::pair<std::string, std::string>> &map,
    std::string_view name) {
    for (const auto &p : map)
        if (p.first == name)
            return true;
    return false;
}
}

TypedObject uniquify_name(
    const TypedObject &obj,
    std::vector<std::pair<std::string, std::string>> &type_map,
    std::vector<std::pair<std::string, std::string>> &renamings) {
    if (!contains_name(type_map, obj.name)) {
        type_map.emplace_back(obj.name, obj.type_name);
        return obj;
    }
    for (int counter = 1;; ++counter) {
        std::string new_name = obj.name + std::to_string(counter);
        if (!contains_name(type_map, new_name)) {
            renamings.emplace_back(obj.name, new_name);
            type_map.emplace_back(new_name, obj.type_name);
            return TypedObject(new_name, obj.type_name);
        }
    }
}
}
