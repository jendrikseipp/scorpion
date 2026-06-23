#ifndef INVARIANTS_CONSTRAINTS_H
#define INVARIANTS_CONSTRAINTS_H

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace translate::invariants {
/*
  A Term is either an object/variable name (string; PDDL variables start
  with '?', objects don't), or an integer denoting an invariant
  parameter (or the COUNTED marker, by convention -1).
*/
using Term = std::variant<std::string, int>;

struct TermHash {
    std::size_t operator()(const Term &t) const noexcept;
};
bool term_less(const Term &a, const Term &b);
bool is_variable_or_param(const Term &t);
bool is_object(const Term &t);
std::string term_to_string(const Term &t);

class EqualityConjunction {
public:
    std::vector<std::pair<Term, Term>> equalities;

    EqualityConjunction() = default;
    explicit EqualityConjunction(std::vector<std::pair<Term, Term>> equalities)
        : equalities(std::move(equalities)) {}

    bool is_consistent();
    const std::unordered_map<Term, Term, TermHash> *get_representative();

private:
    std::optional<bool> consistent_;
    std::unordered_map<Term, Term, TermHash> representative_;
    void compute_representatives();
};

class InequalityDisjunction {
public:
    std::vector<std::pair<Term, Term>> parts;

    InequalityDisjunction() = default;
    explicit InequalityDisjunction(std::vector<std::pair<Term, Term>> parts)
        : parts(std::move(parts)) {}
};

class ConstraintSystem {
public:
    std::vector<std::vector<EqualityConjunction>> equality_DNFs;
    std::vector<InequalityDisjunction> ineq_disjunctions;
    std::vector<std::string> not_constant;

    void add_equality_conjunction(EqualityConjunction c) {
        equality_DNFs.push_back({std::move(c)});
    }
    void add_equality_DNF(std::vector<EqualityConjunction> dnf) {
        equality_DNFs.push_back(std::move(dnf));
    }
    void add_inequality_disjunction(InequalityDisjunction d) {
        ineq_disjunctions.push_back(std::move(d));
    }
    void add_not_constant(std::string s) {
        not_constant.push_back(std::move(s));
    }
    void extend(const ConstraintSystem &other);

    bool is_solvable() const;
};
}

#endif
