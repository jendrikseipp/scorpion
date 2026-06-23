#include "predicate.h"

using namespace std;
namespace translate::pddl {
namespace {
template<class StreamT, class ListT>
void write_args(StreamT &os, const ListT &args) {
    os << "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) os << ", ";
        os << args[i];
    }
    os << ")";
}
}

ostream &operator<<(ostream &os, const Predicate &p) {
    os << p.name;
    write_args(os, p.arguments);
    return os;
}

ostream &operator<<(ostream &os, const Function &f) {
    os << f.name;
    write_args(os, f.arguments);
    if (!f.type_name.empty())
        os << ": " << f.type_name;
    return os;
}
}
