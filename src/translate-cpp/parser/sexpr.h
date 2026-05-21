#ifndef TRANSLATE_PARSER_SEXPR_H
#define TRANSLATE_PARSER_SEXPR_H

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace translate::parser {
/*
  S-expression: either an atom (lower-cased token, as produced by the PDDL
  tokenizer) or a list of nested s-expressions. Recursive variant is fine
  here because std::vector<Sexpr> only needs Sexpr to be a forward-declared
  complete type at instantiation time; the standard allows this for vector.
*/

class Sexpr;
using SexprList = std::vector<Sexpr>;

class Sexpr {
public:
    using Value = std::variant<std::string, SexprList>;

    Sexpr() : value_(std::string{}) {}
    Sexpr(std::string s) : value_(std::move(s)) {}
    Sexpr(SexprList list) : value_(std::move(list)) {}

    bool is_atom() const {
        return std::holds_alternative<std::string>(value_);
    }
    bool is_list() const {
        return std::holds_alternative<SexprList>(value_);
    }
    const std::string &atom() const { return std::get<std::string>(value_); }
    const SexprList &list() const { return std::get<SexprList>(value_); }
    SexprList &list() { return std::get<SexprList>(value_); }

private:
    Value value_;
};

void write_lispified(std::ostream &os, const Sexpr &expr);
std::string lispified(const Sexpr &expr);

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
}

#endif
