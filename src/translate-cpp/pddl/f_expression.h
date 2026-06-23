#ifndef PDDL_F_EXPRESSION_H
#define PDDL_F_EXPRESSION_H

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace translate::pddl {
/*
  Numeric (functional) expressions. The translator only supports numeric
  functions, not object fluents.
*/

class FunctionalExpression;
using FExprPtr = std::shared_ptr<const FunctionalExpression>;

class FunctionalExpression {
public:
    enum class Kind { CONSTANT, PNE };
    virtual ~FunctionalExpression() = default;
    virtual Kind kind() const = 0;
    virtual void dump(std::ostream &os, int indent = 0) const = 0;
};

class NumericConstant final : public FunctionalExpression {
public:
    long long value;

    explicit NumericConstant(long long value) : value(value) {}
    Kind kind() const override { return Kind::CONSTANT; }
    void dump(std::ostream &os, int indent) const override;
};

class PrimitiveNumericExpression final : public FunctionalExpression {
public:
    std::string symbol;
    std::vector<std::string> args;
    std::size_t cached_hash;

    PrimitiveNumericExpression(std::string symbol,
                               std::vector<std::string> args);
    Kind kind() const override { return Kind::PNE; }
    void dump(std::ostream &os, int indent) const override;
    bool operator==(const PrimitiveNumericExpression &other) const;
};

class FunctionAssignment {
public:
    enum class Kind { ASSIGN, INCREASE };
    std::shared_ptr<PrimitiveNumericExpression> fluent;
    FExprPtr expression;

    FunctionAssignment(std::shared_ptr<PrimitiveNumericExpression> fluent,
                       FExprPtr expression)
        : fluent(std::move(fluent)), expression(std::move(expression)) {}
    virtual ~FunctionAssignment() = default;
    virtual Kind kind() const = 0;
    virtual void dump(std::ostream &os, int indent = 0) const;
};

class Assign final : public FunctionAssignment {
public:
    using FunctionAssignment::FunctionAssignment;
    Kind kind() const override { return Kind::ASSIGN; }
};

class Increase final : public FunctionAssignment {
public:
    using FunctionAssignment::FunctionAssignment;
    Kind kind() const override { return Kind::INCREASE; }
};
}

#endif
