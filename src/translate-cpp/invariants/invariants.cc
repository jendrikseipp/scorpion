#include "invariants.h"

#include "invariant_finder.h"

#include "../pddl/action.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../utils/hash.h"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace std;
namespace translate::invariants {
using namespace pddl;

// ----- InvariantPart -------------------------------------------------------

vector<string> InvariantPart::get_parameters(const Literal &literal) const {
    int n = arity();
    vector<string> result(n);
    for (size_t pos = 0; pos < args.size(); ++pos) {
        int v = args[pos];
        if (v == COUNTED)
            continue;
        result[v] = literal.args[pos];
    }
    return result;
}

ConditionPtr InvariantPart::instantiate(
    const vector<string> &parameters_tuple) const {
    vector<string> a;
    a.reserve(args.size());
    for (int v : args) {
        if (v == COUNTED)
            a.push_back("?X");
        else
            a.push_back(parameters_tuple[v]);
    }
    return make_shared<Atom>(predicate, move(a));
}

size_t InvariantPart::get_hash() const noexcept {
    size_t h = hash<string>{}(predicate);
    for (int a : args)
        utils::hash_combine(h, hash<int>{}(a));
    return h;
}

namespace {
/*
  Enumerate all bijections from union(preimgs) to union(imgs) such that
  each preimg maps to its corresponding img. Each mapping is yielded as
  a flat list of (preimg-element, img-element) pairs.
*/
void instantiate_factored_mapping(
    const vector<pair<vector<int>, vector<int>>> &pairs, size_t depth,
    vector<pair<int, int>> &current,
    const function<void(const vector<pair<int, int>> &)> &emit) {
    if (depth == pairs.size()) {
        emit(current);
        return;
    }
    const auto &[preimg, img] = pairs[depth];
    vector<int> perm = img;
    ranges::sort(perm);
    do {
        size_t added = preimg.size();
        for (size_t i = 0; i < preimg.size(); ++i)
            current.emplace_back(preimg[i], perm[i]);
        instantiate_factored_mapping(pairs, depth + 1, current, emit);
        for (size_t i = 0; i < added; ++i)
            current.pop_back();
    } while (next_permutation(perm.begin(), perm.end()));
}
}

void InvariantPart::possible_matches(
    const Literal &own_literal, const Literal &other_literal,
    vector<InvariantPart> &result) const {
    int allowed_omissions =
        static_cast<int>(other_literal.args.size()) - arity();
    if (allowed_omissions != 0 && allowed_omissions != 1)
        return;

    auto own_params = get_parameters(own_literal);
    unordered_map<string, vector<int>> own_arg_to_params;
    for (size_t k = 0; k < own_params.size(); ++k)
        own_arg_to_params[own_params[k]].push_back(static_cast<int>(k));

    unordered_map<string, vector<int>> other_arg_to_pos;
    for (size_t i = 0; i < other_literal.args.size(); ++i)
        other_arg_to_pos[other_literal.args[i]].push_back(static_cast<int>(i));

    vector<pair<vector<int>, vector<int>>> factored;
    int remaining_omissions = allowed_omissions;
    for (auto &[key, other_positions] : other_arg_to_pos) {
        auto it = own_arg_to_params.find(key);
        vector<int> inv_params =
            (it == own_arg_to_params.end()) ? vector<int>{} : it->second;
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
    vector<pair<int, int>> current;
    instantiate_factored_mapping(
        factored, 0, current, [&](const vector<pair<int, int>> &mapping) {
            vector<int> args(other_literal.args.size(), COUNTED);
            int omitted = -1;
            for (const auto &[other_pos, inv_var] : mapping) {
                if (inv_var == COUNTED)
                    omitted = other_pos;
                else
                    args[other_pos] = inv_var;
            }
            result.emplace_back(other_literal.predicate, move(args), omitted);
        });
}

// ----- Invariant -----------------------------------------------------------

Invariant::Invariant(vector<InvariantPart> parts_) {
    // Deduplicate by predicate (matching Python: at most one part per pred).
    sort(parts_.begin(), parts_.end());
    parts_.erase(unique(parts_.begin(), parts_.end()), parts_.end());
    parts = move(parts_);
    compute_predicate_map();
}

Invariant::Invariant(const Invariant &other) : parts(other.parts) {
    compute_predicate_map();
}

Invariant::Invariant(Invariant &&other) noexcept
    : parts(move(other.parts)),
      predicate_to_part_(move(other.predicate_to_part_)) {
    // A vector move transfers the buffer without relocating elements, so the
    // pointers in predicate_to_part_ (which point into `parts`) stay valid --
    // move the map rather than rebuild it. `other` is left with an empty parts
    // vector and an empty map, consistently moved-from.
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
        parts = move(other.parts);
        // See the move constructor: the pointers survive the buffer transfer.
        predicate_to_part_ = move(other.predicate_to_part_);
    }
    return *this;
}

void Invariant::compute_predicate_map() {
    predicate_to_part_.clear();
    predicate_to_part_.reserve(parts.size());
    for (const auto &p : parts)
        predicate_to_part_.emplace_back(p.predicate, &p);
}

const InvariantPart *Invariant::part_or_null(const string &predicate) const {
    // Last match wins, matching the previous map's insert-assign semantics
    // (parts are unique by predicate in practice, so this returns the one
    // part).
    const InvariantPart *found = nullptr;
    for (const auto &[pred, part] : predicate_to_part_)
        if (pred == predicate)
            found = part;
    return found;
}

bool Invariant::operator==(const Invariant &o) const {
    if (parts.size() != o.parts.size())
        return false;
    for (size_t i = 0; i < parts.size(); ++i)
        if (!(parts[i] == o.parts[i]))
            return false;
    return true;
}

size_t Invariant::get_hash() const noexcept {
    size_t h = 0;
    for (const auto &p : parts)
        utils::hash_combine(h, p.get_hash());
    return h;
}

vector<string> Invariant::get_parameters(const Literal &atom) const {
    const InvariantPart *part = part_or_null(atom.predicate);
    if (!part)
        return {};
    return part->get_parameters(atom);
}

EqualityConjunction Invariant::get_cover_equivalence_conjunction(
    const Literal &literal) const {
    const InvariantPart *found = part_or_null(literal.predicate);
    if (!found)
        return {};
    const InvariantPart &part = *found;
    vector<pair<Term, Term>> eqs;
    for (size_t pos = 0; pos < part.args.size(); ++pos) {
        int v = part.args[pos];
        if (v == COUNTED)
            continue;
        eqs.emplace_back(Term(v), Term(literal.args[pos]));
    }
    return EqualityConjunction(move(eqs));
}

namespace {
vector<const Literal *> get_literals(const ConditionPtr &cond) {
    vector<const Literal *> out;
    if (!cond)
        return out;
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

void ensure_inequality(
    ConstraintSystem &system, const Literal &l1, const Literal &l2) {
    if (l1.predicate == l2.predicate && !l1.args.empty()) {
        vector<pair<Term, Term>> parts;
        size_t n = min(l1.args.size(), l2.args.size());
        for (size_t i = 0; i < n; ++i)
            parts.emplace_back(Term(l1.args[i]), Term(l2.args[i]));
        system.add_inequality_disjunction(InequalityDisjunction(move(parts)));
    }
}

void ensure_conjunction_sat(
    ConstraintSystem &system,
    initializer_list<vector<const Literal *>> groups) {
    unordered_map<string, vector<const Literal *>> pos, neg;
    for (const auto &grp : groups) {
        for (const auto *lit : grp) {
            if (lit->predicate == "=") {
                vector<pair<Term, Term>> parts;
                if (lit->args.size() == 2)
                    parts.emplace_back(Term(lit->args[0]), Term(lit->args[1]));
                if (lit->negated()) {
                    system.add_inequality_disjunction(
                        InequalityDisjunction(parts));
                } else {
                    system.add_equality_DNF({EqualityConjunction(parts)});
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
        if (it == neg.end())
            continue;
        for (const auto *p : p_atoms) {
            for (const auto *n : it->second) {
                vector<pair<Term, Term>> parts;
                size_t m = min(p->args.size(), n->args.size());
                for (size_t i = 0; i < m; ++i)
                    parts.emplace_back(Term(n->args[i]), Term(p->args[i]));
                if (!parts.empty())
                    system.add_inequality_disjunction(
                        InequalityDisjunction(move(parts)));
            }
        }
    }
}
}

bool Invariant::check_balance(
    BalanceChecker &checker,
    const function<void(Invariant)> &enqueue_func) const {
    // Collect actions threatening any of our parts, first occurrence winning;
    // the checker's epoch stamp deduplicates without per-call allocations
    // (schema-heavy domains run this for every candidate over tens of
    // thousands of actions -- a per-call hash set dominated the phase).
    vector<int> actions_to_check;
    checker.begin_action_set();
    vector<InvariantPart> sorted_parts = parts;
    sort(sorted_parts.begin(), sorted_parts.end());
    for (const auto &part : sorted_parts) {
        for (int a : checker.get_threats(part.predicate)) {
            if (checker.insert_action(a))
                actions_to_check.push_back(a);
        }
    }
    // For determinism we randomize order; without RNG seeded the same as
    // Python (314159), we draw uniformly via mt19937. The user accepted
    // semantic equivalence (not byte-identical).
    while (!actions_to_check.empty()) {
        int pos = checker.next_index(actions_to_check.size());
        swap(actions_to_check[pos], actions_to_check.back());
        int action = actions_to_check.back();
        actions_to_check.pop_back();
        if (operator_too_heavy(checker.heavy_action(action)))
            return false;
        if (operator_unbalanced(checker.action(action), enqueue_func))
            return false;
    }
    return true;
}

bool Invariant::operator_too_heavy(const Action &h_action) const {
    vector<const Effect *> add_effects;
    for (const auto &eff : h_action.effects) {
        if (!eff.literal)
            continue;
        const auto &lit = static_cast<const Literal &>(*eff.literal);
        if (!lit.negated() && part_or_null(lit.predicate))
            add_effects.push_back(&eff);
    }
    if (add_effects.size() <= 1)
        return false;
    for (size_t i = 0; i < add_effects.size(); ++i) {
        for (size_t j = i + 1; j < add_effects.size(); ++j) {
            const auto &lit1 =
                static_cast<const Literal &>(*add_effects[i]->literal);
            const auto &lit2 =
                static_cast<const Literal &>(*add_effects[j]->literal);
            ConstraintSystem system;
            ensure_inequality(system, lit1, lit2);
            system.add_equality_conjunction(
                get_cover_equivalence_conjunction(lit1));
            system.add_equality_conjunction(
                get_cover_equivalence_conjunction(lit2));
            auto pre_lits = get_literals(h_action.precondition);
            auto cond1 = get_literals(add_effects[i]->condition);
            auto cond2 = get_literals(add_effects[j]->condition);
            vector<const Literal *> n1, n2;
            auto neg1 = lit1.negate();
            auto neg2 = lit2.negate();
            n1.push_back(static_cast<const Literal *>(neg1.get()));
            n2.push_back(static_cast<const Literal *>(neg2.get()));
            ensure_conjunction_sat(system, {pre_lits, cond1, cond2, n1, n2});
            if (system.is_solvable())
                return true;
        }
    }
    return false;
}

bool Invariant::operator_unbalanced(
    const Action &action, const function<void(Invariant)> &enqueue_func) const {
    vector<const Effect *> add_effects, del_effects;
    for (const auto &eff : action.effects) {
        if (!eff.literal)
            continue;
        const auto &lit = static_cast<const Literal &>(*eff.literal);
        if (!part_or_null(lit.predicate))
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
    const vector<const Effect *> &del_effects,
    const function<void(Invariant)> &enqueue_func) const {
    const auto &add_lit = static_cast<const Literal &>(*add_effect.literal);
    ProducedMap produced;
    auto extend_produced = [&](const ConditionPtr &c) {
        for (const auto *lit : get_literals(c))
            produced[lit->predicate].push_back({lit, lit->negated()});
    };
    extend_produced(action.precondition);
    extend_produced(add_effect.condition);
    // The add effect's negation is produced too: same literal, flipped sign.
    produced[add_lit.predicate].push_back({&add_lit, !add_lit.negated()});
    auto add_cover = get_cover_equivalence_conjunction(add_lit);

    ConstraintSystem param_system;
    auto cover_copy = add_cover;
    const auto *repr_ptr = cover_copy.get_representative();
    unordered_map<Term, Term, TermHash> repr;
    if (repr_ptr)
        repr = *repr_ptr;
    vector<string> params;
    params.reserve(action.parameters.size() + add_effect.parameters.size());
    for (const auto &p : action.parameters)
        params.push_back(p.name);
    for (const auto &p : add_effect.parameters)
        params.push_back(p.name);
    for (const auto &p : params) {
        Term t(p);
        auto it = repr.find(t);
        Term r = (it == repr.end()) ? t : it->second;
        if (is_variable_or_param(r))
            param_system.add_not_constant(p);
    }
    for (size_t i = 0; i < params.size(); ++i) {
        for (size_t j = i + 1; j < params.size(); ++j) {
            Term ti(params[i]), tj(params[j]);
            auto ri_it = repr.find(ti);
            auto rj_it = repr.find(tj);
            Term ri = (ri_it == repr.end()) ? ti : ri_it->second;
            Term rj = (rj_it == repr.end()) ? tj : rj_it->second;
            if (!(ri == rj)) {
                vector<pair<Term, Term>> parts = {{ti, tj}};
                param_system.add_inequality_disjunction(
                    InequalityDisjunction(move(parts)));
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
    const ProducedMap &produced, const EqualityConjunction &add_cover,
    const ConstraintSystem &param_system) const {
    const auto &add_lit = static_cast<const Literal &>(*add_effect.literal);
    const auto &del_lit = static_cast<const Literal &>(*del_effect.literal);

    // Build balance system. The delete effect's condition literals keep their
    // sign; the deleted literal itself participates negated (sign flip only,
    // no materialized negation).
    ConstraintSystem balance_system;
    vector<ProducedLit> all_lits;
    for (const auto *lit : get_literals(del_effect.condition))
        all_lits.push_back({lit, lit->negated()});
    all_lits.push_back({&del_lit, !del_lit.negated()});
    for (const auto &[lit, lit_negated] : all_lits) {
        vector<EqualityConjunction> possibilities;
        const auto *group = produced.find_group(lit->predicate);
        if (!group)
            return false;
        for (const auto &m : *group) {
            if (m.negated != lit_negated)
                continue;
            vector<pair<Term, Term>> eqs;
            size_t n = min(lit->args.size(), m.lit->args.size());
            for (size_t i = 0; i < n; ++i)
                eqs.emplace_back(Term(lit->args[i]), Term(m.lit->args[i]));
            possibilities.emplace_back(move(eqs));
        }
        if (possibilities.empty())
            return false;
        balance_system.add_equality_DNF(move(possibilities));
    }
    ensure_inequality(balance_system, add_lit, del_lit);

    ConstraintSystem system;
    system.add_equality_conjunction(add_cover);
    system.add_equality_conjunction(get_cover_equivalence_conjunction(del_lit));
    system.extend(balance_system);
    system.extend(param_system);
    return system.is_solvable();
}

void Invariant::refine_candidate(
    const Effect &add_effect, const Action &action,
    const function<void(Invariant)> &enqueue_func) const {
    const auto &add_lit = static_cast<const Literal &>(*add_effect.literal);
    const InvariantPart *found = part_or_null(add_lit.predicate);
    if (!found)
        return;
    const InvariantPart &part = *found;
    for (const auto &del_eff : action.effects) {
        if (!del_eff.literal)
            continue;
        const auto &lit = static_cast<const Literal &>(*del_eff.literal);
        if (!lit.negated())
            continue;
        if (part_or_null(lit.predicate))
            continue;
        vector<InvariantPart> matches;
        part.possible_matches(add_lit, lit, matches);
        for (auto &m : matches) {
            vector<InvariantPart> new_parts = parts;
            new_parts.push_back(move(m));
            // Deduplicate by predicate: skip if duplicate predicate.
            set<string> preds;
            bool dup = false;
            for (const auto &p : new_parts) {
                if (!preds.insert(p.predicate).second) {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
            enqueue_func(Invariant(move(new_parts)));
        }
    }
}
}
