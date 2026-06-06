#include "invariants.h"

#include "../pddl/action.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "invariant_finder.h"

#include <algorithm>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace translate::invariants {
using namespace pddl;

// ----- InvariantPart -------------------------------------------------------

std::vector<std::string> InvariantPart::get_parameters(
    const Literal &literal) const {
    int n = arity();
    std::vector<std::string> result(n);
    for (std::size_t pos = 0; pos < args.size(); ++pos) {
        int v = args[pos];
        if (v == COUNTED) continue;
        result[v] = literal.args[pos];
    }
    return result;
}

ConditionPtr InvariantPart::instantiate(
    const std::vector<std::string> &parameters_tuple) const {
    std::vector<std::string> a;
    a.reserve(args.size());
    for (int v : args) {
        if (v == COUNTED) a.push_back("?X");
        else a.push_back(parameters_tuple[v]);
    }
    return std::make_shared<Atom>(predicate, std::move(a));
}

std::size_t InvariantPart::hash() const noexcept {
    std::size_t h = std::hash<std::string>{}(predicate);
    for (int a : args)
        h ^= std::hash<int>{}(a) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

namespace {
/*
  Enumerate all bijections from union(preimgs) to union(imgs) such that
  each preimg maps to its corresponding img. Each mapping is yielded as
  a flat list of (preimg-element, img-element) pairs.
*/
void instantiate_factored_mapping(
    const std::vector<std::pair<std::vector<int>,
                                std::vector<int>>> &pairs,
    std::size_t depth, std::vector<std::pair<int, int>> &current,
    const std::function<void(const std::vector<std::pair<int, int>> &)> &emit) {
    if (depth == pairs.size()) { emit(current); return; }
    const auto &[preimg, img] = pairs[depth];
    std::vector<int> perm = img;
    std::ranges::sort(perm);
    do {
        std::size_t added = preimg.size();
        for (std::size_t i = 0; i < preimg.size(); ++i)
            current.emplace_back(preimg[i], perm[i]);
        instantiate_factored_mapping(pairs, depth + 1, current, emit);
        for (std::size_t i = 0; i < added; ++i) current.pop_back();
    } while (std::next_permutation(perm.begin(), perm.end()));
}
}

void InvariantPart::possible_matches(
    const Literal &own_literal, const Literal &other_literal,
    std::vector<InvariantPart> &result) const {
    int allowed_omissions =
        static_cast<int>(other_literal.args.size()) - arity();
    if (allowed_omissions != 0 && allowed_omissions != 1) return;

    auto own_params = get_parameters(own_literal);
    std::unordered_map<std::string, std::vector<int>> own_arg_to_params;
    for (std::size_t k = 0; k < own_params.size(); ++k)
        own_arg_to_params[own_params[k]].push_back(static_cast<int>(k));

    std::unordered_map<std::string, std::vector<int>> other_arg_to_pos;
    for (std::size_t i = 0; i < other_literal.args.size(); ++i)
        other_arg_to_pos[other_literal.args[i]].push_back(static_cast<int>(i));

    std::vector<std::pair<std::vector<int>, std::vector<int>>> factored;
    int remaining_omissions = allowed_omissions;
    for (auto &[key, other_positions] : other_arg_to_pos) {
        auto it = own_arg_to_params.find(key);
        std::vector<int> inv_params = (it == own_arg_to_params.end())
                                          ? std::vector<int>{}
                                          : it->second;
        int len_diff = static_cast<int>(inv_params.size()) -
                       static_cast<int>(other_positions.size());
        if (len_diff >= 1 || len_diff <= -2 ||
            (len_diff == -1 && remaining_omissions == 0))
            return;
        if (len_diff != 0) {
            inv_params.push_back(COUNTED);
            remaining_omissions = 0;
        }
        factored.emplace_back(other_positions, inv_params);
    }
    std::vector<std::pair<int, int>> current;
    instantiate_factored_mapping(factored, 0, current,
        [&](const std::vector<std::pair<int, int>> &mapping) {
            std::vector<int> args(other_literal.args.size(), COUNTED);
            int omitted = -1;
            for (const auto &[other_pos, inv_var] : mapping) {
                if (inv_var == COUNTED) omitted = other_pos;
                else args[other_pos] = inv_var;
            }
            result.emplace_back(other_literal.predicate, std::move(args),
                                omitted);
        });
}

// ----- Invariant -----------------------------------------------------------

Invariant::Invariant(std::vector<InvariantPart> parts_) {
    // Deduplicate by predicate (matching Python: at most one part per pred).
    std::sort(parts_.begin(), parts_.end());
    parts_.erase(std::unique(parts_.begin(), parts_.end()), parts_.end());
    parts = std::move(parts_);
    compute_predicate_map();
}

Invariant::Invariant(const Invariant &other) : parts(other.parts) {
    compute_predicate_map();
}

Invariant::Invariant(Invariant &&other) noexcept : parts(std::move(other.parts)) {
    compute_predicate_map();
    other.predicate_to_part_.clear();
}

Invariant &Invariant::operator=(const Invariant &other) {
    if (this != &other) {
        parts = other.parts;
        compute_predicate_map();
    }
    return *this;
}

Invariant &Invariant::operator=(Invariant &&other) noexcept {
    if (this != &other) {
        parts = std::move(other.parts);
        compute_predicate_map();
        other.predicate_to_part_.clear();
    }
    return *this;
}

void Invariant::compute_predicate_map() {
    predicate_to_part_.clear();
    for (const auto &p : parts)
        predicate_to_part_[p.predicate] = &p;
}

bool Invariant::operator==(const Invariant &o) const {
    if (parts.size() != o.parts.size()) return false;
    for (std::size_t i = 0; i < parts.size(); ++i)
        if (!(parts[i] == o.parts[i])) return false;
    return true;
}

std::size_t Invariant::hash() const noexcept {
    std::size_t h = 0;
    for (const auto &p : parts)
        h ^= p.hash() + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

std::vector<std::string> Invariant::get_parameters(const Literal &atom) const {
    auto it = predicate_to_part_.find(atom.predicate);
    if (it == predicate_to_part_.end()) return {};
    return it->second->get_parameters(atom);
}

EqualityConjunction Invariant::get_cover_equivalence_conjunction(
    const Literal &literal) const {
    auto it = predicate_to_part_.find(literal.predicate);
    if (it == predicate_to_part_.end()) return {};
    const InvariantPart &part = *it->second;
    std::vector<std::pair<Term, Term>> eqs;
    for (std::size_t pos = 0; pos < part.args.size(); ++pos) {
        int v = part.args[pos];
        if (v == COUNTED) continue;
        eqs.emplace_back(Term(v), Term(literal.args[pos]));
    }
    return EqualityConjunction(std::move(eqs));
}

namespace {
std::vector<const Literal *> get_literals(const ConditionPtr &cond) {
    std::vector<const Literal *> out;
    if (!cond) return out;
    if (cond->kind() == Condition::Kind::ATOM ||
        cond->kind() == Condition::Kind::NEGATED_ATOM) {
        out.push_back(static_cast<const Literal *>(cond.get()));
    } else if (cond->kind() == Condition::Kind::CONJUNCTION) {
        for (const auto &p : cond->parts()) {
            if (p && (p->kind() == Condition::Kind::ATOM ||
                      p->kind() == Condition::Kind::NEGATED_ATOM)) {
                out.push_back(static_cast<const Literal *>(p.get()));
            }
        }
    }
    return out;
}

void ensure_inequality(ConstraintSystem &system,
                       const Literal &l1, const Literal &l2) {
    if (l1.predicate == l2.predicate && !l1.args.empty()) {
        std::vector<std::pair<Term, Term>> parts;
        std::size_t n = std::min(l1.args.size(), l2.args.size());
        for (std::size_t i = 0; i < n; ++i)
            parts.emplace_back(Term(l1.args[i]), Term(l2.args[i]));
        system.add_inequality_disjunction(
            InequalityDisjunction(std::move(parts)));
    }
}

void ensure_conjunction_sat(ConstraintSystem &system,
                            std::initializer_list<std::vector<const Literal *>>
                                groups) {
    std::unordered_map<std::string, std::vector<const Literal *>> pos, neg;
    for (const auto &grp : groups) {
        for (const auto *lit : grp) {
            if (lit->predicate == "=") {
                std::vector<std::pair<Term, Term>> parts;
                if (lit->args.size() == 2)
                    parts.emplace_back(Term(lit->args[0]), Term(lit->args[1]));
                if (lit->negated()) {
                    system.add_inequality_disjunction(
                        InequalityDisjunction(parts));
                } else {
                    system.add_equality_DNF(
                        {EqualityConjunction(parts)});
                }
            } else if (lit->negated()) {
                neg[lit->predicate].push_back(lit);
            } else {
                pos[lit->predicate].push_back(lit);
            }
        }
    }
    for (auto &[pred, p_atoms] : pos) {
        auto it = neg.find(pred);
        if (it == neg.end()) continue;
        for (const auto *p : p_atoms) {
            for (const auto *n : it->second) {
                std::vector<std::pair<Term, Term>> parts;
                std::size_t m = std::min(p->args.size(), n->args.size());
                for (std::size_t i = 0; i < m; ++i)
                    parts.emplace_back(Term(n->args[i]), Term(p->args[i]));
                if (!parts.empty())
                    system.add_inequality_disjunction(
                        InequalityDisjunction(std::move(parts)));
            }
        }
    }
}
}

bool Invariant::check_balance(
    BalanceChecker &checker,
    const std::function<void(Invariant)> &enqueue_func) const {
    // Collect actions threatening any of our parts.
    std::vector<const Action *> actions_to_check;
    std::unordered_set<const Action *> seen;
    std::vector<InvariantPart> sorted_parts = parts;
    std::sort(sorted_parts.begin(), sorted_parts.end());
    for (const auto &part : sorted_parts) {
        for (const auto *a : checker.get_threats(part.predicate)) {
            if (seen.insert(a).second) actions_to_check.push_back(a);
        }
    }
    // For determinism we randomize order; without RNG seeded the same as
    // Python (314159), we draw uniformly via std::mt19937. The user accepted
    // semantic equivalence (not byte-identical).
    while (!actions_to_check.empty()) {
        int pos = checker.next_index(actions_to_check.size());
        std::swap(actions_to_check[pos], actions_to_check.back());
        const Action *action = actions_to_check.back();
        actions_to_check.pop_back();
        const Action *heavy = checker.get_heavy_action(action);
        if (operator_too_heavy(*heavy)) return false;
        if (operator_unbalanced(*action, enqueue_func)) return false;
    }
    return true;
}

bool Invariant::operator_too_heavy(const Action &h_action) const {
    std::vector<const Effect *> add_effects;
    for (const auto &eff : h_action.effects) {
        if (!eff.literal) continue;
        const auto &lit = static_cast<const Literal &>(*eff.literal);
        if (!lit.negated() &&
            predicate_to_part_.contains(lit.predicate))
            add_effects.push_back(&eff);
    }
    if (add_effects.size() <= 1) return false;
    for (std::size_t i = 0; i < add_effects.size(); ++i) {
        for (std::size_t j = i + 1; j < add_effects.size(); ++j) {
            const auto &lit1 = static_cast<const Literal &>(*add_effects[i]->literal);
            const auto &lit2 = static_cast<const Literal &>(*add_effects[j]->literal);
            ConstraintSystem system;
            ensure_inequality(system, lit1, lit2);
            system.add_equality_conjunction(
                get_cover_equivalence_conjunction(lit1));
            system.add_equality_conjunction(
                get_cover_equivalence_conjunction(lit2));
            auto pre_lits = get_literals(h_action.precondition);
            auto cond1 = get_literals(add_effects[i]->condition);
            auto cond2 = get_literals(add_effects[j]->condition);
            std::vector<const Literal *> n1, n2;
            auto neg1 = lit1.negate();
            auto neg2 = lit2.negate();
            n1.push_back(static_cast<const Literal *>(neg1.get()));
            n2.push_back(static_cast<const Literal *>(neg2.get()));
            ensure_conjunction_sat(system, {pre_lits, cond1, cond2, n1, n2});
            if (system.is_solvable()) return true;
        }
    }
    return false;
}

bool Invariant::operator_unbalanced(
    const Action &action,
    const std::function<void(Invariant)> &enqueue_func) const {
    std::vector<const Effect *> add_effects, del_effects;
    for (const auto &eff : action.effects) {
        if (!eff.literal) continue;
        const auto &lit = static_cast<const Literal &>(*eff.literal);
        if (!predicate_to_part_.contains(lit.predicate))
            continue;
        (lit.negated() ? del_effects : add_effects).push_back(&eff);
    }
    for (const auto *eff : add_effects)
        if (add_effect_unbalanced(action, *eff, del_effects, enqueue_func))
            return true;
    return false;
}

bool Invariant::add_effect_unbalanced(
    const Action &action, const Effect &add_effect,
    const std::vector<const Effect *> &del_effects,
    const std::function<void(Invariant)> &enqueue_func) const {
    const auto &add_lit = static_cast<const Literal &>(*add_effect.literal);
    std::unordered_map<std::string, std::vector<ConditionPtr>> produced;
    auto extend_produced = [&](const ConditionPtr &c) {
        for (const auto *lit : get_literals(c))
            produced[lit->predicate].push_back(
                lit->negated()
                    ? std::static_pointer_cast<const Condition>(
                          std::make_shared<NegatedAtom>(lit->predicate,
                                                        lit->args))
                    : std::static_pointer_cast<const Condition>(
                          std::make_shared<Atom>(lit->predicate, lit->args)));
    };
    extend_produced(action.precondition);
    extend_produced(add_effect.condition);
    {
        auto neg = add_lit.negate();
        const auto &nl = static_cast<const Literal &>(*neg);
        produced[nl.predicate].push_back(neg);
    }
    auto add_cover = get_cover_equivalence_conjunction(add_lit);

    ConstraintSystem param_system;
    auto cover_copy = add_cover;
    const auto *repr_ptr = cover_copy.get_representative();
    std::unordered_map<Term, Term, TermHash> repr;
    if (repr_ptr) repr = *repr_ptr;
    std::vector<std::string> params;
    for (const auto &p : action.parameters) params.push_back(p.name);
    for (const auto &p : add_effect.parameters) params.push_back(p.name);
    for (const auto &p : params) {
        Term t(p);
        auto it = repr.find(t);
        Term r = (it == repr.end()) ? t : it->second;
        if (is_variable_or_param(r)) param_system.add_not_constant(p);
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
        for (std::size_t j = i + 1; j < params.size(); ++j) {
            Term ti(params[i]), tj(params[j]);
            auto ri_it = repr.find(ti); auto rj_it = repr.find(tj);
            Term ri = (ri_it == repr.end()) ? ti : ri_it->second;
            Term rj = (rj_it == repr.end()) ? tj : rj_it->second;
            if (!(ri == rj)) {
                std::vector<std::pair<Term, Term>> parts = {{ti, tj}};
                param_system.add_inequality_disjunction(
                    InequalityDisjunction(std::move(parts)));
            }
        }
    }
    for (const auto *del : del_effects) {
        if (balances(*del, add_effect, produced, add_cover, param_system))
            return false;
    }
    refine_candidate(add_effect, action, enqueue_func);
    return true;
}

bool Invariant::balances(
    const Effect &del_effect, const Effect &add_effect,
    const std::unordered_map<std::string,
                             std::vector<ConditionPtr>> &produced,
    const EqualityConjunction &add_cover,
    const ConstraintSystem &param_system) const {
    const auto &add_lit = static_cast<const Literal &>(*add_effect.literal);
    const auto &del_lit = static_cast<const Literal &>(*del_effect.literal);

    // Build balance system.
    ConstraintSystem balance_system;
    auto cond_lits = get_literals(del_effect.condition);
    std::vector<const Literal *> all_lits = cond_lits;
    auto del_neg = del_lit.negate();
    all_lits.push_back(static_cast<const Literal *>(del_neg.get()));
    for (const auto *lit : all_lits) {
        std::vector<EqualityConjunction> possibilities;
        auto it = produced.find(lit->predicate);
        if (it == produced.end()) return false;
        for (const auto &match : it->second) {
            const auto &m = static_cast<const Literal &>(*match);
            if (m.negated() != lit->negated()) continue;
            std::vector<std::pair<Term, Term>> eqs;
            std::size_t n = std::min(lit->args.size(), m.args.size());
            for (std::size_t i = 0; i < n; ++i)
                eqs.emplace_back(Term(lit->args[i]), Term(m.args[i]));
            possibilities.emplace_back(std::move(eqs));
        }
        if (possibilities.empty()) return false;
        balance_system.add_equality_DNF(std::move(possibilities));
    }
    ensure_inequality(balance_system, add_lit, del_lit);

    ConstraintSystem system;
    system.add_equality_conjunction(add_cover);
    system.add_equality_conjunction(
        get_cover_equivalence_conjunction(del_lit));
    system.extend(balance_system);
    system.extend(param_system);
    return system.is_solvable();
}

void Invariant::refine_candidate(
    const Effect &add_effect, const Action &action,
    const std::function<void(Invariant)> &enqueue_func) const {
    const auto &add_lit = static_cast<const Literal &>(*add_effect.literal);
    auto pit = predicate_to_part_.find(add_lit.predicate);
    if (pit == predicate_to_part_.end()) return;
    const InvariantPart &part = *pit->second;
    for (const auto &del_eff : action.effects) {
        if (!del_eff.literal) continue;
        const auto &lit = static_cast<const Literal &>(*del_eff.literal);
        if (!lit.negated()) continue;
        if (predicate_to_part_.contains(lit.predicate))
            continue;
        std::vector<InvariantPart> matches;
        part.possible_matches(add_lit, lit, matches);
        for (auto &m : matches) {
            std::vector<InvariantPart> new_parts = parts;
            new_parts.push_back(std::move(m));
            // Deduplicate by predicate: skip if duplicate predicate.
            std::set<std::string> preds;
            bool dup = false;
            for (const auto &p : new_parts) {
                if (!preds.insert(p.predicate).second) { dup = true; break; }
            }
            if (dup) continue;
            enqueue_func(Invariant(std::move(new_parts)));
        }
    }
}
}
