#ifndef TRANSLATE_GROUNDING_PROGRAM_H
#define TRANSLATE_GROUNDING_PROGRAM_H

#include "symbols.h"

#include "algorithms/small_vector.h"

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace translate::grounding {
/*
  A Datalog atom argument is one of:
   - a constant (object name, no '?' prefix)
   - a variable name (starts with '?')
   - an integer position (only after variables_to_numbers, used inside
     model.cc for effect-arg references).

  Constants and variables are interned into the process-wide symbol
  table and stored as a 4-byte id (>= 0). Positions are stored inline
  as negative values, p encoded as -(p+1). This replaces the former
  std::variant<std::string,int> (~40 bytes + a heap buffer per atom),
  which dominated peak memory on hard-to-ground instances (~10M atoms).

  Equality and hashing use the raw id directly (equal names always
  intern to the same id). Ordering that must stay byte-compatible with
  the Python translator resolves ids back to names via arg_to_string.
*/
struct Arg {
    int v = 0; // >= 0: symbol id; < 0: position p stored as -(p+1)

    Arg() = default;
    Arg(const std::string &s) : v(symbols().intern(s)) {}
    Arg(const char *s) : v(symbols().intern(std::string(s))) {}
    explicit Arg(int position) : v(-(position + 1)) {}

    bool is_symbol() const noexcept { return v >= 0; }
    bool is_position() const noexcept { return v < 0; }
    int position() const noexcept { return -v - 1; }
    const std::string &name() const { return symbols().name(v); }

    bool operator==(const Arg &o) const noexcept { return v == o.v; }
};

/*
  Argument lists for grounding atoms. Predicate/rule arities are almost always
  tiny (<= 4), but the model build creates millions of ground atoms, so a
  std::vector here means millions of heap allocations. SmallVector keeps the
  arguments inline for the common small case while staying the same size as a
  std::vector, so it spills to the heap only for the rare high-arity atom.
*/
using ArgList = small_vector::SmallVector<Arg, 4>;

inline std::string arg_to_string(const Arg &a) {
    return a.is_symbol() ? a.name() : std::to_string(a.position());
}
inline bool is_variable(const Arg &a) {
    if (!a.is_symbol()) return false;
    const std::string &s = a.name();
    return !s.empty() && s.front() == '?';
}
inline bool is_constant(const Arg &a) {
    if (!a.is_symbol()) return false;
    const std::string &s = a.name();
    return s.empty() || s.front() != '?';
}
inline bool is_int(const Arg &a) { return a.is_position(); }

struct Atom {
    // Interned predicate-name id (shares symbols() with Arg). Interning makes
    // the per-atom hash/equality used by the grounding dedup an int compare
    // and avoids copying the predicate string into every ground atom.
    int predicate = 0;
    ArgList args;

    Atom() = default;
    Atom(int predicate, ArgList args)
        : predicate(predicate), args(std::move(args)) {}
    Atom(const std::string &predicate, ArgList args)
        : predicate(symbols().intern(predicate)), args(std::move(args)) {}

    const std::string &predicate_name() const { return symbols().name(predicate); }

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
