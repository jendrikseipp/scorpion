#include "f_expression.h"

#include <functional>

using namespace std;
namespace translate::pddl {
namespace {
inline void hash_combine(size_t &seed, size_t v) {
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}

void NumericConstant::dump(ostream &os, int indent) const {
    os << string(indent * 2, ' ') << "NumericConstant " << value << "\n";
}

PrimitiveNumericExpression::PrimitiveNumericExpression(
    string symbol, vector<string> args)
    : symbol(move(symbol)), args(move(args)), cached_hash(0) {
    size_t h = hash<string>{}(this->symbol);
    for (const auto &a : this->args)
        hash_combine(h, hash<string>{}(a));
    cached_hash = h;
}

void PrimitiveNumericExpression::dump(ostream &os, int indent) const {
    os << string(indent * 2, ' ') << "PNE " << symbol << "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) os << ", ";
        os << args[i];
    }
    os << ")\n";
}

bool PrimitiveNumericExpression::operator==(
    const PrimitiveNumericExpression &other) const {
    return symbol == other.symbol && args == other.args;
}

void FunctionAssignment::dump(ostream &os, int indent) const {
    os << string(indent * 2, ' ')
       << (kind() == Kind::ASSIGN ? "Assign" : "Increase") << "\n";
    if (fluent) fluent->dump(os, indent + 1);
    if (expression) expression->dump(os, indent + 1);
}
}
