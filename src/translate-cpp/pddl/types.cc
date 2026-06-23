#include "types.h"

using namespace std;
namespace translate::pddl {
string type_predicate_name(string_view type_name) {
    string result = "type@";
    result.append(type_name);
    return result;
}

ostream &operator<<(ostream &os, const Type &t) {
    return os << t.name;
}

ostream &operator<<(ostream &os, const TypedObject &o) {
    return os << o.name << ": " << o.type_name;
}

TypedObject uniquify_name(
    const TypedObject &obj,
    unordered_map<string, string> &type_map,
    unordered_map<string, string> &renamings) {
    if (!type_map.contains(obj.name)) {
        type_map.emplace(obj.name, obj.type_name);
        return obj;
    }
    for (int counter = 1;; ++counter) {
        string new_name = obj.name + to_string(counter);
        if (!type_map.contains(new_name)) {
            renamings.emplace(obj.name, new_name);
            type_map.emplace(new_name, obj.type_name);
            return TypedObject(new_name, obj.type_name);
        }
    }
}
}
