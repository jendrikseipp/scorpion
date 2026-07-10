#ifndef INVARIANTS_INVARIANTS_H
#define INVARIANTS_INVARIANTS_H

#include "constraints.h"

#include "../pddl/action.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace translate::invariants {
class BalanceChecker; // fwd

inline constexpr int COUNTED = -1;

class InvariantPart {
public:
    std::string predicate;
    std::vector<int> args; // Each entry: invariant parameter index, or COUNTED.
    int omitted_pos; // -1 if no position is omitted.

    InvariantPart() : omitted_pos(-1) {
    }
    InvariantPart(std::string predicate, std::vector<int> args, int omitted)
        : predicate(std::move(predicate)),
          args(std::move(args)),
          omitted_pos(omitted) {
    }

    int arity() const {
        return omitted_pos < 0 ? static_cast<int>(args.size())
                               : static_cast<int>(args.size()) - 1;
    }

    bool operator==(const InvariantPart &o) const {
        return predicate == o.predicate && args == o.args;
    }
    bool operator<(const InvariantPart &o) const {
        if (predicate != o.predicate)
            return predicate < o.predicate;
        return args < o.args;
    }

    /*
      Map invariant parameter index -> the corresponding string argument
      of `literal` (the kth invariant parameter gets the argument at
      position k in `literal.args`, skipping omitted_pos).
    */
    std::vector<std::string> get_parameters(const pddl::Literal &literal) const;

    /*
      Build a ground (well, lifted: COUNTED positions become "?X") atom
      after replacing each invariant-parameter position with the
      corresponding `parameters_tuple` entry.
    */
    pddl::ConditionPtr instantiate(
        const std::vector<std::string> &parameters_tuple) const;

    /*
      Compute candidate invariant parts that could balance `own_literal`
      with `other_literal`. Mirrors Python InvariantPart.possible_matches.
    */
    void possible_matches(
        const pddl::Literal &own_literal, const pddl::Literal &other_literal,
        std::vector<InvariantPart> &result) const;

    std::size_t get_hash() const noexcept;
};

struct InvariantPartHash {
    std::size_t operator()(const InvariantPart &p) const noexcept {
        return p.get_hash();
    }
};

class Invariant {
public:
    std::vector<InvariantPart> parts; // sorted, deduplicated by predicate

    Invariant() = default;
    explicit Invariant(std::vector<InvariantPart> parts_);
    Invariant(const Invariant &other);
    Invariant(Invariant &&other) noexcept;
    Invariant &operator=(const Invariant &other);
    Invariant &operator=(Invariant &&other) noexcept;

    int arity() const {
        return parts.empty() ? 0 : parts.front().arity();
    }
    bool operator==(const Invariant &o) const;
    std::size_t get_hash() const noexcept;

    /*
      Run the H2-style balance check for this invariant candidate.
      Calls `enqueue_func(refined_candidate)` to push refinements when
      the check fails for an add effect. Returns true if all checks
      pass and this candidate is a true invariant.
    */
    bool check_balance(
        BalanceChecker &checker,
        const std::function<void(Invariant)> &enqueue_func) const;

    /*
      Returns the parameters dictionary that aligns this invariant to
      an atom (only the part matching atom.predicate is considered).
    */
    std::vector<std::string> get_parameters(const pddl::Literal &atom) const;

private:
    void compute_predicate_map();
    // The part (if any) whose predicate matches, or nullptr. An invariant has a
    // handful of parts, so predicate_to_part_ is a flat vector scanned linearly
    // rather than a hash map: it allocates one buffer instead of a node per
    // part (the map was rebuilt on every candidate copy) and avoids hashing
    // predicate strings on the many balance-check probes.
    const InvariantPart *part_or_null(const std::string &predicate) const;
    std::vector<std::pair<std::string, const InvariantPart *>>
        predicate_to_part_;

    EqualityConjunction get_cover_equivalence_conjunction(
        const pddl::Literal &literal) const;

    bool operator_too_heavy(const pddl::Action &heavy_action) const;
    bool operator_unbalanced(
        const pddl::Action &action,
        const std::function<void(Invariant)> &enqueue_func) const;
    bool add_effect_unbalanced(
        const pddl::Action &action, const pddl::Effect &add_effect,
        const std::vector<const pddl::Effect *> &del_effects,
        const std::function<void(Invariant)> &enqueue_func) const;
    /*
      A literal "produced" by the action (from its precondition or an effect
      condition), as a pointer plus an explicit sign. Keeping the sign outside
      the literal lets the negation of an existing literal be represented
      without materializing a new Atom: the balance check only reads
      (predicate, args, sign), and it runs per (candidate x action x effect).
    */
    struct ProducedLit {
        const pddl::Literal *lit;
        bool negated;
    };
    /*
      Produced literals grouped by predicate. A flat vector rather than a hash
      map: an action produces only a handful of predicates, so linear lookup
      beats a per-predicate node allocation plus predicate-string hashing on
      every (candidate x add-effect) rebuild.
    */
    class ProducedMap {
    public:
        // Insert-or-access the group for `predicate` (used while building).
        std::vector<ProducedLit> &operator[](const std::string &predicate) {
            for (auto &e : entries_)
                if (e.first == predicate)
                    return e.second;
            entries_.emplace_back(predicate, std::vector<ProducedLit>{});
            return entries_.back().second;
        }
        // The group for `predicate`, or nullptr if none was produced.
        const std::vector<ProducedLit> *find_group(
            const std::string &predicate) const {
            for (const auto &e : entries_)
                if (e.first == predicate)
                    return &e.second;
            return nullptr;
        }

    private:
        std::vector<std::pair<std::string, std::vector<ProducedLit>>> entries_;
    };
    bool balances(
        const pddl::Effect &del_effect, const pddl::Effect &add_effect,
        const ProducedMap &produced, const EqualityConjunction &add_cover,
        const ConstraintSystem &param_system) const;
    void refine_candidate(
        const pddl::Effect &add_effect, const pddl::Action &action,
        const std::function<void(Invariant)> &enqueue_func) const;
};

struct InvariantHash {
    std::size_t operator()(const Invariant &i) const noexcept {
        return i.get_hash();
    }
};
}

#endif
