#ifndef TRANSLATE_GROUNDING_PROGRAM_H
#define TRANSLATE_GROUNDING_PROGRAM_H

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace translate::grounding {
/*
  A Datalog atom argument is either:
   - a constant (object name, no '?' prefix)
   - a variable name (starts with '?')
   - an integer position (only after variables_to_numbers, used inside
     build_model.cc for effect-arg references).
  We represent the union as std::variant<std::string, int>.
*/
using Arg = std::variant<std::string, int>;

inline bool is_variable(const Arg &a) {
    if (auto *s = std::get_if<std::string>(&a))
        return !s->empty() && s->front() == '?';
    return false;
}
inline bool is_constant(const Arg &a) {
    if (auto *s = std::get_if<std::string>(&a))
        return s->empty() || s->front() != '?';
    return false;
}
inline bool is_int(const Arg &a) {
    return std::holds_alternative<int>(a);
}

struct Atom {
    std::string predicate;
    std::vector<Arg> args;

    Atom() = default;
    Atom(std::string predicate, std::vector<Arg> args)
        : predicate(std::move(predicate)), args(std::move(args)) {}

    bool operator==(const Atom &other) const {
        return predicate == other.predicate && args == other.args;
    }
    bool operator<(const Atom &other) const;
};

struct AtomHash {
    std::size_t operator()(const Atom &a) const noexcept;
};

std::ostream &operator<<(std::ostream &os, const Atom &a);

enum class RuleKind { NONE, JOIN, PRODUCT, PROJECT };

struct Rule {
    std::vector<Atom> conditions;
    Atom effect;
    RuleKind kind = RuleKind::NONE;

    // Rename duplicate occurrences of a variable in `effect` and each
    // condition; emit equality conditions linking them. Returns true if
    // any rewriting happened.
    bool rename_duplicate_variables();
};

std::ostream &operator<<(std::ostream &os, const Rule &r);

class Program {
public:
    std::vector<Atom> facts;
    std::vector<Rule> rules;
    std::unordered_set<std::string> objects;

    void add_fact(Atom atom);
    void add_rule(Rule rule);

    // Mint a fresh auxiliary predicate name "p$<n>".
    std::string new_predicate_name();

    /*
      Bring the program into normal form:
        - effect variables must occur in the body (otherwise an @object
          predicate is added),
        - no variable appears twice in the same atom (otherwise equality
          conditions are added),
        - no rules with empty body (such rules are converted to facts).
    */
    void normalize();

    void dump(std::ostream &os) const;

private:
    int next_aux_id_ = 0;
};

// Variable names occurring in any of the given atoms (those starting with '?').
std::unordered_set<std::string> get_variables(const std::vector<Atom> &atoms);
std::unordered_set<std::string> get_variables(const Atom &atom);
}

#endif
