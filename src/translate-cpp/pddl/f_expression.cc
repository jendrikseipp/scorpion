#include "f_expression.h"

#include <functional>

namespace translate::pddl {
namespace {
inline void hash_combine(std::size_t &seed, std::size_t v) {
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}

void NumericConstant::dump(std::ostream &os, int indent) const {
    os << std::string(indent * 2, ' ') << "NumericConstant " << value << "\n";
}

PrimitiveNumericExpression::PrimitiveNumericExpression(
    std::string symbol, std::vector<std::string> args)
    : symbol(std::move(symbol)), args(std::move(args)), cached_hash(0) {
    std::size_t h = std::hash<std::string>{}(this->symbol);
    for (const auto &a : this->args)
        hash_combine(h, std::hash<std::string>{}(a));
    cached_hash = h;
}

void PrimitiveNumericExpression::dump(std::ostream &os, int indent) const {
    os << std::string(indent * 2, ' ') << "PNE " << symbol << "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) os << ", ";
        os << args[i];
    }
    os << ")\n";
}

bool PrimitiveNumericExpression::operator==(
    const PrimitiveNumericExpression &other) const {
    return symbol == other.symbol && args == other.args;
}

void FunctionAssignment::dump(std::ostream &os, int indent) const {
    os << std::string(indent * 2, ' ')
       << (kind() == Kind::ASSIGN ? "Assign" : "Increase") << "\n";
    if (fluent) fluent->dump(os, indent + 1);
    if (expression) expression->dump(os, indent + 1);
}
}
