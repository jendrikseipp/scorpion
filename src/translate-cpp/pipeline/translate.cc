#include "translate.h"

#include "../translate_options.h"

#include "../axioms/axiom_rules.h"
#include "../fact_groups/fact_groups.h"
#include "../grounding/build.h"
#include "../grounding/model.h"
#include "../grounding/program.h"
#include "../grounding/split.h"
#include "../instantiate/instantiate.h"
#include "../normalize/normalize.h"
#include "../sas/sas_task.h"
#include "../simplify/simplify.h"
#include "../simplify/variable_order.h"
#include "../utils/timer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace std;
namespace translate::pipeline {
using namespace pddl;
using sas::PrePost;
using sas::SASAxiom;
using sas::SASGoal;
using sas::SASInit;
using sas::SASMutexGroup;
using sas::SASOperator;
using sas::SASTask;
using sas::SASVariables;
using sas::VarVal;

namespace {
// FactId of a ground atom via the fluent-fact table, or -1 if it is not a
// fluent fact (e.g. a static init atom). Used only on the non-hot ConditionPtr
// paths (fact groups, init, goal, axioms, mutexes); the hot operator loop
// carries FactIds directly in its GroundLiterals.
FactId fact_id_of(const Literal &lit, const FactMap &ids) {
    GroundKey key;
    key.predicate = lit.predicate_id;
    key.args.reserve(lit.args.size());
    for (const auto &a : lit.args)
        key.args.push_back(grounding::symbols().intern(a));
    const FactId *id = ids.find(key);
    return id ? *id : -1;
}

// FactId -> its SAS (var, val) representations. Indexed by FactId; an empty
// entry means the fact is absent from these groups. Keying operator-literal
// lookups by FactId makes them an int index rather than building and hashing a
// per-literal string key.
using FactToVarVals = vector<vector<VarVal>>;

struct StripsToSas {
    vector<int> ranges;
    FactToVarVals factvals;
};

StripsToSas build_dictionary(
    const vector<vector<ConditionPtr>> &groups, size_t num_facts,
    const FactMap &fluent_ids, bool assert_partial) {
    StripsToSas out;
    out.factvals.resize(num_facts);
    out.ranges.reserve(groups.size());
    for (size_t var = 0; var < groups.size(); ++var) {
        for (size_t val = 0; val < groups[var].size(); ++val) {
            const auto &atom = static_cast<const Atom &>(*groups[var][val]);
            FactId f = fact_id_of(atom, fluent_ids);
            if (f >= 0)
                out.factvals[f].push_back(
                    {static_cast<int>(var), static_cast<int>(val)});
        }
        out.ranges.push_back(static_cast<int>(groups[var].size()) + 1);
    }
    if (assert_partial) {
        for (const auto &vv : out.factvals)
            if (vv.size() > 1)
                throw runtime_error(
                    "use-partial-encoding: atom must be in at most one group");
    }
    return out;
}

// Facts (FDR pairs) implied by a fact: in every state containing p, all pairs
// in implied_facts[p] must also hold. Used only with
// --add-implied-preconditions.
using ImpliedFacts = map<VarVal, vector<VarVal>>;

/*
  Port of Python's build_implied_facts (main.py). The only exploited case is:
  p encodes a STRIPS proposition X, q encodes "not Y", and X and Y are mutex.
  For q to encode "not Y", Y must form a fact group of size 1 ("lonely"); then
  every other fact in Y's mutex group implies "not Y" = (Y's var, 1).
*/
ImpliedFacts build_implied_facts(
    const fact_groups::ComputedGroups &groups, const StripsToSas &strips_to_sas,
    const FactMap &fluent_ids) {
    // Lonely propositions: size-1 fact groups -> their SAS variable number
    // (the proposition is encoded as (var, 0); see build_dictionary).
    unordered_map<FactId, int> lonely;
    for (size_t var = 0; var < groups.groups.size(); ++var) {
        if (groups.groups[var].size() == 1) {
            const auto &prop =
                static_cast<const Atom &>(*groups.groups[var][0]);
            FactId f = fact_id_of(prop, fluent_ids);
            if (f >= 0)
                lonely[f] = static_cast<int>(var);
        }
    }
    ImpliedFacts implied;
    for (const auto &mutex_group : groups.mutex_groups) {
        for (size_t i = 0; i < mutex_group.size(); ++i) {
            const auto &prop = static_cast<const Atom &>(*mutex_group[i]);
            auto lit = lonely.find(fact_id_of(prop, fluent_ids));
            if (lit == lonely.end())
                continue;
            VarVal prop_is_false{lit->second, 1};
            for (size_t j = 0; j < mutex_group.size(); ++j) {
                if (j == i)
                    continue;
                const auto &other = static_cast<const Atom &>(*mutex_group[j]);
                FactId f = fact_id_of(other, fluent_ids);
                if (f < 0)
                    continue;
                for (const auto &fact : strips_to_sas.factvals[f])
                    implied[fact].push_back(prop_is_false);
            }
        }
    }
    return implied;
}

// Convert atom-based literals (goal, axioms) to GroundLiterals so they share
// the operator translation path. Every kept literal is a fluent fact.
vector<GroundLiteral> to_ground_literals(
    const vector<ConditionPtr> &lits, const FactMap &fluent_ids) {
    vector<GroundLiteral> out;
    out.reserve(lits.size());
    for (const auto &c : lits) {
        const auto &lit = static_cast<const Literal &>(*c);
        FactId f = fact_id_of(lit, fluent_ids);
        out.push_back({f, lit.negated()});
    }
    return out;
}

/*
  A small map from an int variable to Value, kept sorted by variable in a flat
  vector. Replaces the per-operator unordered_map<int, ...> maps of the
  "Translating task" phase: a flat vector collapses each map to a single buffer
  instead of a control block plus a node allocation per entry, which removes the
  malloc/free churn that dominated that phase. It offers the map-like subset the
  callers use (find/[]/erase/iterate). Every consumer sorts the resulting pairs
  before emitting them, so the deterministic sorted-by-variable iteration order
  leaves the output byte-identical.
*/
template<typename Value>
class FlatMap {
public:
    using value_type = pair<int, Value>;
    using iterator = typename vector<value_type>::iterator;
    using const_iterator = typename vector<value_type>::const_iterator;

    iterator begin() {
        return entries_.begin();
    }
    iterator end() {
        return entries_.end();
    }
    const_iterator begin() const {
        return entries_.begin();
    }
    const_iterator end() const {
        return entries_.end();
    }
    size_t size() const {
        return entries_.size();
    }
    bool empty() const {
        return entries_.empty();
    }

    iterator find(int var) {
        auto it = lower_bound_(var);
        return (it != entries_.end() && it->first == var) ? it : entries_.end();
    }
    const_iterator find(int var) const {
        auto it = lower_bound_(var);
        return (it != entries_.end() && it->first == var) ? it : entries_.end();
    }
    bool contains(int var) const {
        return find(var) != entries_.end();
    }

    // Insert-or-access, like std::map::operator[], keeping entries sorted.
    Value &operator[](int var) {
        auto it = lower_bound_(var);
        if (it != entries_.end() && it->first == var)
            return it->second;
        return entries_.insert(it, {var, Value{}})->second;
    }

    void erase(const_iterator it) {
        entries_.erase(it);
    }
    void erase(int var) {
        auto it = find(var);
        if (it != entries_.end())
            entries_.erase(it);
    }

private:
    // Sorted by variable; linear scan for lookup/insert, but these maps hold a
    // handful of entries so this beats a hash map on both time and allocation.
    vector<value_type> entries_;

    iterator lower_bound_(int var) {
        return ranges::lower_bound(entries_, var, {}, &value_type::first);
    }
    const_iterator lower_bound_(int var) const {
        return ranges::lower_bound(entries_, var, {}, &value_type::first);
    }
};

// Value set for a variable under construction: sorted, duplicate-free. A
// SmallVector keeps the common small sets (single-valued positive literals,
// binary-variable negatives) inline, avoiding a per-value heap allocation.
using ValueSet = small_vector::SmallVector<int, 4>;
// var -> allowed values (behaves like the std::set<int> it replaces: same
// ascending iteration order, no red-black-tree node per value).
using CondMap = FlatMap<ValueSet>;
// var -> value: a partial assignment (operator/effect conditions).
using VarMap = FlatMap<int>;

inline bool value_set_contains(const ValueSet &s, int v) {
    return binary_search(s.begin(), s.end(), v);
}

// Intersect `condition` with the positive literals: each pins its variable(s)
// to a single value. Returns false if that contradicts an existing entry (the
// condition is then unsatisfiable).
bool add_positive_conditions(
    const vector<GroundLiteral> &conditions, const FactToVarVals &factvals,
    CondMap &condition) {
    for (const auto &lit : conditions) {
        if (lit.negated)
            continue;
        const auto &varvals = factvals[lit.fact];
        if (varvals.empty())
            continue; // static (absent from these groups)
        for (const auto &[var, val] : varvals) {
            auto cit = condition.find(var);
            if (cit != condition.end()) {
                if (!value_set_contains(cit->second, val))
                    return false;
                cit->second = {val};
            } else {
                condition[var] = {val};
            }
        }
    }
    return true;
}

// Refine `condition` with the negative literals: each excludes one value from
// its variable's domain. Returns false if that empties a domain.
bool add_negative_conditions(
    const vector<GroundLiteral> &conditions, const FactToVarVals &factvals,
    const vector<int> &ranges, CondMap &condition) {
    for (const auto &lit : conditions) {
        if (!lit.negated)
            continue;
        const auto &varvals = factvals[lit.fact];
        if (varvals.empty())
            continue;
        bool done = false;
        CondMap new_condition;
        for (const auto &[var, val] : varvals) {
            // Ascending order => sorted and duplicate-free by construction.
            ValueSet poss_vals;
            poss_vals.reserve(ranges[var] - 1);
            for (int v = 0; v < ranges[var]; ++v)
                if (v != val)
                    poss_vals.push_back(v);
            auto cit = condition.find(var);
            if (cit == condition.end()) {
                new_condition[var] = move(poss_vals);
            } else {
                done = true;
                ValueSet intersection;
                set_intersection(
                    cit->second.begin(), cit->second.end(), poss_vals.begin(),
                    poss_vals.end(), back_inserter(intersection));
                if (intersection.empty())
                    return false;
                cit->second = move(intersection);
            }
        }
        if (!done && !new_condition.empty()) {
            // Pick the smallest-cardinality candidate, breaking ties by the
            // order the representations appear in the dictionary. Python sorts
            // new_condition.items() by cardinality with a stable sort, so among
            // equal sizes it keeps the first-inserted (= dictionary) order. We
            // must iterate the dictionary entries (it->second) here rather than
            // the unordered new_condition map, whose iteration order is
            // unspecified -- otherwise full-encoding facts with several equal-
            // size representations pick a different variable than Python.
            int best_var = -1;
            size_t best_size = SIZE_MAX;
            for (const auto &[var, val] : varvals) {
                auto nit = new_condition.find(var);
                if (nit != new_condition.end() &&
                    nit->second.size() < best_size) {
                    best_size = nit->second.size();
                    best_var = var;
                }
            }
            condition[best_var] = move(new_condition[best_var]);
        }
    }
    return true;
}

// Multiply-out a per-variable value-set condition into the list of concrete
// (var -> value) assignments (the DNF terms). Variables are expanded
// smallest-domain first, matching Python's ordering.
vector<VarMap> expand_condition_map(const CondMap &condition) {
    vector<pair<int, ValueSet>> sorted_conds(
        condition.begin(), condition.end());
    ranges::sort(sorted_conds, [](const auto &a, const auto &b) {
        return a.second.size() < b.second.size();
    });
    vector<VarMap> flat_conds = {{}};
    for (const auto &[var, vals] : sorted_conds) {
        if (vals.size() == 1) {
            int val = vals[0];
            for (auto &cond : flat_conds)
                cond[var] = val;
        } else {
            vector<VarMap> new_conds;
            for (const auto &cond : flat_conds) {
                for (int val : vals) {
                    auto nc = cond;
                    nc[var] = val;
                    new_conds.push_back(move(nc));
                }
            }
            flat_conds = move(new_conds);
        }
    }
    return flat_conds;
}

optional<vector<VarMap>> translate_strips_conditions_aux(
    const vector<GroundLiteral> &conditions, const FactToVarVals &factvals,
    const vector<int> &ranges) {
    CondMap condition;
    if (!add_positive_conditions(conditions, factvals, condition))
        return nullopt;
    if (!add_negative_conditions(conditions, factvals, ranges, condition))
        return nullopt;
    return expand_condition_map(condition);
}

optional<vector<VarMap>> translate_strips_conditions(
    const vector<GroundLiteral> &conditions, const FactToVarVals &factvals,
    const vector<int> &ranges, const FactToVarVals &mutex_factvals,
    const vector<int> &mutex_ranges) {
    if (conditions.empty())
        return vector<VarMap>{{}};
    auto mtx = translate_strips_conditions_aux(
        conditions, mutex_factvals, mutex_ranges);
    if (!mtx)
        return nullopt;
    return translate_strips_conditions_aux(conditions, factvals, ranges);
}

optional<vector<VarMap>> negate_and_translate_condition(
    const vector<vector<GroundLiteral>> &condition,
    const FactToVarVals &factvals, const vector<int> &ranges,
    const FactToVarVals &mutex_factvals, const vector<int> &mutex_ranges) {
    vector<VarMap> negation;
    // An empty group inside `condition` means "always satisfied" — the
    // negation is unsatisfiable. (Matches Python's `if [] in condition`.)
    for (const auto &group : condition)
        if (group.empty())
            return nullopt;
    /*
      No add-effect conditions at all means there is no condition under
      which an add fires, so the "no-add-fires" disjunction is vacuously
      true. Python returns `[{}]` (a single empty assignment) in this
      case; we must do the same so that del effects without a matching
      add still produce a none-of-those transition.
    */
    if (condition.empty()) {
        negation.emplace_back();
        return negation;
    }
    // Iterate over the cartesian product of literals.
    vector<size_t> idx(condition.size(), 0);
    while (true) {
        vector<GroundLiteral> combination;
        for (size_t i = 0; i < condition.size(); ++i)
            combination.push_back(condition[i][idx[i]].negate());
        auto cond = translate_strips_conditions(
            combination, factvals, ranges, mutex_factvals, mutex_ranges);
        if (cond)
            for (auto &c : *cond)
                negation.push_back(move(c));
        // Increment.
        size_t k = condition.size();
        while (k > 0) {
            --k;
            if (++idx[k] < condition[k].size())
                break;
            idx[k] = 0;
            if (k == 0)
                return negation.empty() ? nullopt : make_optional(negation);
        }
        if (k == 0 && idx[0] == 0)
            break;
    }
    return negation.empty() ? nullopt : make_optional(negation);
}

optional<SASOperator> build_sas_operator(
    const string &name, VarMap condition,
    FlatMap<FlatMap<vector<VarMap>>> &effects_by_variable, int cost,
    const vector<int> &ranges, const ImpliedFacts &implied_facts) {
    VarMap prevail_and_pre = condition;
    // Facts implied by the operator's (prevail + pre) condition. Computed from
    // the full condition before the effects loop erases entries from it.
    set<VarVal> implied_precondition;
    if (get_options().add_implied_preconditions) {
        for (const auto &[var, val] : condition) {
            auto it = implied_facts.find(VarVal{var, val});
            if (it != implied_facts.end())
                implied_precondition.insert(
                    it->second.begin(), it->second.end());
        }
    }
    vector<PrePost> pre_post;
    for (auto &[var, effects_on_var] : effects_by_variable) {
        int orig_pre = -1;
        auto cit = condition.find(var);
        if (cit != condition.end())
            orig_pre = cit->second;
        bool added = false;
        for (auto &[post, eff_conds] : effects_on_var) {
            int pre = orig_pre;
            if (pre == post)
                continue;
            /*
              prune_stupid_effect_conditions for binary variables.
              If `var` has range 2 and no effect produces the dual value
              (1 - post), then any condition entry on (var, 1 - post) is
              redundant: when the effect fires it must be because the var
              currently has the dual value (otherwise it would already be
              at `post`). Mirrors Python's prune_stupid_effect_conditions
              and is required for matching Python's operator encoding on
              binary-var-heavy domains (satellite, airport, miconic-adl).
            */
            if (ranges[var] == 2 && !effects_on_var.contains(1 - post)) {
                int dual_val = 1 - post;
                bool sweep_to_empty = false;
                for (auto &eff_cond : eff_conds) {
                    auto it = eff_cond.find(var);
                    if (it != eff_cond.end() && it->second == dual_val)
                        eff_cond.erase(it);
                    if (eff_cond.empty()) {
                        sweep_to_empty = true;
                        break;
                    }
                }
                if (sweep_to_empty) {
                    eff_conds.clear();
                    eff_conds.emplace_back();
                }
            }
            // If the (prevail+pre) condition implies the variable already holds
            // the value being changed away from (1 - post), make that an
            // explicit precondition. Mirrors Python's add_implied_preconditions
            // branch, which is gated on ranges[var] == 2 (independently of the
            // prune_stupid_effect_conditions simplification above).
            if (ranges[var] == 2 && get_options().add_implied_preconditions &&
                pre == -1 &&
                implied_precondition.contains(VarVal{var, 1 - post})) {
                pre = 1 - post;
            }
            for (auto &eff_cond : eff_conds) {
                vector<VarVal> filtered;
                bool contradict = false;
                for (const auto &[cv, cval] : eff_cond) {
                    auto pit = prevail_and_pre.find(cv);
                    if (pit != prevail_and_pre.end()) {
                        if (pit->second != cval) {
                            contradict = true;
                            break;
                        }
                    } else {
                        filtered.emplace_back(cv, cval);
                    }
                }
                if (contradict)
                    continue;
                ranges::sort(filtered);
                pre_post.emplace_back(var, pre, post, move(filtered));
                added = true;
            }
        }
        if (added)
            condition.erase(var);
    }
    if (pre_post.empty() && !get_options().keep_no_ops)
        return nullopt;
    /*
      Canonicalize pre_post: sort by (var, pre, post, cond) and dedupe.
      Matches Python's SASOperator._canonical_pre_post. We do this once
      at construction; downstream simplify and variable_order remaps
      preserve the order without re-sorting, and SASOperator::output
      writes in whatever order is current. This keeps byte-level
      compatibility with the Python translator's output -- previously
      output re-sorted by post-remap variable numbers, which produced
      a different ordering than Python's canonical-then-remap flow.
    */
    ranges::sort(pre_post);
    pre_post.erase(unique(pre_post.begin(), pre_post.end()), pre_post.end());
    SASOperator op;
    op.name = name;
    for (const auto &[v, val] : condition)
        op.prevail.emplace_back(v, val);
    ranges::sort(op.prevail);
    op.pre_post = move(pre_post);
    op.cost = cost;
    return op;
}

optional<SASOperator> translate_strips_operator_aux(
    const PropositionalAction &op, const FactToVarVals &factvals,
    const vector<int> &ranges, const FactToVarVals &mutex_factvals,
    const vector<int> &mutex_ranges, const VarMap &condition,
    const ImpliedFacts &implied_facts) {
    FlatMap<FlatMap<vector<VarMap>>> effects_by_variable;
    // Per-operator by-var lookups. FlatMap (a sorted vector) rather than a
    // std::map / unordered_map: these hold a handful of entries but are built
    // fresh for every operator, so a tree/hash node per entry across millions
    // of operators is pure allocator churn. Iteration order is not used (the
    // del loop below walks del_var_order); both are pure var-keyed lookups.
    FlatMap<vector<vector<GroundLiteral>>> add_conds_by_var;

    for (const auto &[conds, fact] : op.add_effects) {
        auto eff_cond_list = translate_strips_conditions(
            conds, factvals, ranges, mutex_factvals, mutex_ranges);
        if (!eff_cond_list)
            continue;
        const auto &varvals = factvals[fact.fact];
        if (varvals.empty())
            continue;
        for (const auto &[var, val] : varvals) {
            for (const auto &ec : *eff_cond_list)
                effects_by_variable[var][val].push_back(ec);
            add_conds_by_var[var].push_back(conds);
        }
    }
    // Collect del effects. Python stores the translated effect-condition dicts
    // by reference and shares the SAME dict objects across all full-encoding
    // representations of a deleted fact (list.extend of the same objects). The
    // none-of-those loop below then mutates them in place (cond[var] = val), so
    // the "deleted value was true" guard ACCUMULATES across a fact's
    // representations in the order the variables are first encountered. We
    // reproduce this exactly with shared condition maps processed in insertion
    // order; the accumulation is what makes the encoding byte-identical to
    // Python under --full-encoding (e.g. cavediving-14-adl).
    using CondPtr = shared_ptr<VarMap>;
    vector<int> del_var_order;
    FlatMap<vector<pair<int, CondPtr>>> del_by_var;
    for (const auto &[conds, fact] : op.del_effects) {
        auto eff_cond_list = translate_strips_conditions(
            conds, factvals, ranges, mutex_factvals, mutex_ranges);
        if (!eff_cond_list)
            continue;
        const auto &varvals = factvals[fact.fact];
        if (varvals.empty())
            continue;
        // One shared condition object per translated effect-condition, reused
        // across every representation of this deleted fact.
        vector<CondPtr> shared;
        shared.reserve(eff_cond_list->size());
        for (const auto &ec : *eff_cond_list)
            shared.push_back(make_shared<VarMap>(ec));
        for (const auto &[var, val] : varvals) {
            if (!del_by_var.contains(var))
                del_var_order.push_back(var);
            for (const auto &sp : shared)
                del_by_var[var].emplace_back(val, sp);
        }
    }
    // For each del-effect var (in insertion order), add the var=none_of_those
    // effect guarded by "the deleted value held and no add effect triggers".
    for (int var : del_var_order) {
        auto no_add = negate_and_translate_condition(
            add_conds_by_var[var], factvals, ranges, mutex_factvals,
            mutex_ranges);
        if (!no_add)
            continue;
        int none_of_those = ranges[var] - 1;
        for (auto &[val, cond_ptr] : del_by_var[var]) {
            auto &cond = *cond_ptr;
            auto cit = cond.find(var);
            if (cit != cond.end() && cit->second != val)
                continue;
            cond[var] = val; // mutate the shared condition (guards accumulate)
            for (const auto &no_add_cond : *no_add) {
                VarMap new_cond = cond;
                bool bad = false;
                for (const auto &[cv, cval] : no_add_cond) {
                    auto pit = new_cond.find(cv);
                    if (pit != new_cond.end() && pit->second != cval) {
                        bad = true;
                        break;
                    }
                    new_cond[cv] = cval;
                }
                if (!bad)
                    effects_by_variable[var][none_of_those].push_back(
                        move(new_cond));
            }
        }
    }
    return build_sas_operator(
        op.name, condition, effects_by_variable, op.cost, ranges,
        implied_facts);
}

vector<SASOperator> translate_strips_operator(
    const PropositionalAction &op, const FactToVarVals &factvals,
    const vector<int> &ranges, const FactToVarVals &mutex_factvals,
    const vector<int> &mutex_ranges, const ImpliedFacts &implied_facts) {
    vector<SASOperator> result;
    auto conds = translate_strips_conditions(
        op.precondition, factvals, ranges, mutex_factvals, mutex_ranges);
    if (!conds)
        return result;
    for (const auto &c : *conds) {
        auto op_out = translate_strips_operator_aux(
            op, factvals, ranges, mutex_factvals, mutex_ranges, c,
            implied_facts);
        if (op_out)
            result.push_back(move(*op_out));
    }
    return result;
}

vector<SASAxiom> translate_strips_axiom(
    const PropositionalAxiom &ax, const FactToVarVals &factvals,
    const vector<int> &ranges, const FactToVarVals &mutex_factvals,
    const vector<int> &mutex_ranges, const FactMap &fluent_ids) {
    vector<SASAxiom> out;
    auto conds = translate_strips_conditions(
        to_ground_literals(ax.condition, fluent_ids), factvals, ranges,
        mutex_factvals, mutex_ranges);
    if (!conds)
        return out;
    if (!ax.effect)
        return out;
    FactId ef = fact_id_of(*ax.effect, fluent_ids);
    if (ef < 0 || factvals[ef].size() != 1)
        return out;
    VarVal eff = factvals[ef].front();
    for (const auto &c : *conds) {
        SASAxiom sa;
        for (const auto &[v, val] : c)
            sa.condition.emplace_back(v, val);
        ranges::sort(sa.condition);
        sa.effect = eff;
        out.push_back(move(sa));
    }
    return out;
}

SASTask trivial_task(bool solvable) {
    SASTask t;
    t.variables.ranges = {2};
    t.variables.axiom_layers = {-1};
    t.variables.value_names = {{"Atom dummy(val1)", "Atom dummy(val2)"}};
    t.init.values = {0};
    t.goal.pairs = {{0, solvable ? 0 : 1}};
    t.metric = true;
    return t;
}

// Positive-atom shells of the goal's negated literals; fact-group selection
// drops these so a negated goal fact is not put in a mutex group with the
// facts it excludes.
AtomSet collect_negative_in_goal(const vector<ConditionPtr> &goal) {
    AtomSet negative_in_goal;
    for (const auto &g : goal) {
        if (!g)
            continue;
        const auto &lit = static_cast<const Literal &>(*g);
        if (lit.negated())
            negative_in_goal.insert(
                make_shared<const Atom>(lit.predicate, lit.args));
    }
    return negative_in_goal;
}

// Initial SAS state: every variable starts at its "none of those" value, then
// each true init atom overrides the variables encoding it.
SASInit build_sas_init(
    const StripsToSas &dict, const Task &task, const FactMap &fluent_ids) {
    SASInit sas_init;
    sas_init.values.assign(dict.ranges.size(), 0);
    for (size_t v = 0; v < dict.ranges.size(); ++v)
        sas_init.values[v] = dict.ranges[v] - 1;
    for (const auto &elem : task.init) {
        if (!holds_alternative<shared_ptr<const Atom>>(elem))
            continue;
        const auto &ap = get<shared_ptr<const Atom>>(elem);
        if (!ap)
            continue;
        FactId f = fact_id_of(*ap, fluent_ids);
        if (f < 0)
            continue;
        for (const auto &[var, val] : dict.factvals[f])
            sas_init.values[var] = val;
    }
    return sas_init;
}

// Per-variable axiom layer (-1 for non-derived), keyed off each layered
// axiom effect's SAS variable.
vector<int> build_axiom_layers(
    const StripsToSas &dict, const axioms::AxiomLayering &layering,
    const FactMap &fluent_ids) {
    vector<int> axiom_layers(dict.ranges.size(), -1);
    for (const auto &[effect, layer] : layering.axiom_layers) {
        FactId f = fact_id_of(*effect, fluent_ids);
        if (f < 0 || dict.factvals[f].empty())
            continue;
        axiom_layers[dict.factvals[f].front().first] = layer;
    }
    return axiom_layers;
}

// SAS mutex groups: only under partial encoding, and only for groups whose
// facts each map to a single (var, val) pair.
vector<SASMutexGroup> build_sas_mutexes(
    const fact_groups::ComputedGroups &groups, const StripsToSas &dict,
    const FactMap &fluent_ids, bool use_partial) {
    vector<SASMutexGroup> sas_mutexes;
    if (!use_partial)
        return sas_mutexes;
    for (const auto &grp : groups.mutex_groups) {
        vector<VarVal> facts;
        for (const auto &f : grp) {
            if (!f)
                continue;
            FactId fid = fact_id_of(static_cast<const Atom &>(*f), fluent_ids);
            if (fid < 0 || dict.factvals[fid].size() != 1)
                continue;
            facts.push_back(dict.factvals[fid].front());
        }
        if (facts.size() >= 2)
            sas_mutexes.emplace_back(move(facts));
    }
    return sas_mutexes;
}

// Sort operators by (name, prevail, pre_post) at SAS construction time --
// before simplify and variable_order touch the task -- matching Python's
// SASTask.__init__ ordering exactly. variable_order's remap then renames var
// numbers without resorting, so the final operator order reflects this
// pre-remap canonical sort.
//
// Sort operator INDICES, then apply the permutation once: sorting ints keeps
// introsort's swaps cheap and cache-friendly, and each 88-byte SASOperator is
// moved exactly once (in the rebuild) instead of on every swap -- ~20% off the
// sort on operator-heavy tasks. Operator names are unique, so the order is
// fully determined (byte-identical to the direct sort).
void sort_operators_canonically(vector<SASOperator> &operators) {
    vector<int> order(operators.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    ranges::sort(order, [&](int a, int b) {
        const SASOperator &oa = operators[a];
        const SASOperator &ob = operators[b];
        if (oa.name != ob.name)
            return oa.name < ob.name;
        if (oa.prevail != ob.prevail)
            return oa.prevail < ob.prevail;
        return oa.pre_post < ob.pre_post;
    });
    vector<SASOperator> sorted;
    sorted.reserve(operators.size());
    for (int i : order)
        sorted.push_back(move(operators[i]));
    operators = move(sorted);
}
}

SASTask pddl_to_sas(Task &task) {
    // Label each phase with the Python translator's wording and print
    // the "[%.3fs CPU, %.3fs wall-clock]" suffix so Lab's stock
    // translator parser captures them as translator_time_<label>.
    auto phase = [](const char *label, auto fn) {
        utils::PhaseTimer t;
        auto v = fn();
        cout << label << ": " << t.str() << endl;
        return v;
    };
    cout << "Instantiating..." << endl;
    auto prog = phase("Generating Datalog program", [&] {
        return grounding::build_program(task);
    });
    phase("Normalizing Datalog program", [&] {
        grounding::split_rules(prog);
        return 0;
    });
    auto model = phase(
        "Computing model", [&] { return grounding::compute_model(prog); });
    auto inst = phase("Completing instantiation", [&] {
        return instantiate::instantiate(task, model, prog.predicate_roles);
    });
    // The grounded model and the Datalog program are only needed through
    // instantiation. Release them now (they can be hundreds of MB on large
    // tasks) so the memory-heavy STRIPS->SAS phases below don't hold them --
    // on logistics/blocksworld-large the peak occurs during translation, not
    // grounding, so this directly lowers peak RSS.
    model = vector<grounding::Atom>{};
    prog = grounding::Program{};

    if (!inst.relaxed_reachable) {
        cout << "No relaxed solution! Generating unsolvable task..." << endl;
        return trivial_task(false);
    }
    if (!inst.instantiated_goal) {
        cout << "Trivially false goal! Generating unsolvable task..." << endl;
        return trivial_task(false);
    }
    AtomSet negative_in_goal =
        collect_negative_in_goal(*inst.instantiated_goal);

    cout << "Computing fact groups..." << endl;
    auto groups = phase("Computing fact groups", [&] {
        return fact_groups::compute_groups(
            task, inst.fluent_facts, &inst.reachable_action_parameters,
            negative_in_goal);
    });

    bool use_partial = get_options().use_partial_encoding;
    const FactMap &fluent_ids = inst.fluent_fact_ids;
    size_t num_facts = inst.fact_by_id.size();
    auto strips_to_sas =
        build_dictionary(groups.groups, num_facts, fluent_ids, use_partial);
    auto mutex_dict =
        build_dictionary(groups.mutex_groups, num_facts, fluent_ids, false);

    // Facts implied by other facts (only used by --add-implied-preconditions).
    ImpliedFacts implied_facts;
    if (get_options().add_implied_preconditions)
        implied_facts = build_implied_facts(groups, strips_to_sas, fluent_ids);

    // Build init.
    SASInit sas_init = build_sas_init(strips_to_sas, task, fluent_ids);
    // Build goal.
    auto goal_gl = to_ground_literals(*inst.instantiated_goal, fluent_ids);
    auto goal_conds = translate_strips_conditions(
        goal_gl, strips_to_sas.factvals, strips_to_sas.ranges,
        mutex_dict.factvals, mutex_dict.ranges);
    if (!goal_conds) {
        cout << "Goal violates a mutex! Generating unsolvable task..." << endl;
        return trivial_task(false);
    }
    if (goal_conds->size() != 1)
        throw runtime_error("Negative goal not supported");
    SASGoal sas_goal;
    for (const auto &[v, val] : goal_conds->front())
        sas_goal.pairs.emplace_back(v, val);
    ranges::sort(sas_goal.pairs);
    if (sas_goal.pairs.empty()) {
        cout << "Empty goal! Generating solvable task..." << endl;
        return trivial_task(true);
    }

    // Process axioms and compute axiom layers.
    auto axiom_layering = phase("Processing axioms", [&] {
        return axioms::handle_axioms(
            inst.instantiated_actions, inst.instantiated_axioms,
            *inst.instantiated_goal, inst.fact_by_id,
            get_options().layer_strategy);
    });

    // Build operators.
    vector<SASOperator> sas_operators;
    phase("Translating task", [&] {
        for (const auto &op : inst.instantiated_actions) {
            auto sub = translate_strips_operator(
                op, strips_to_sas.factvals, strips_to_sas.ranges,
                mutex_dict.factvals, mutex_dict.ranges, implied_facts);
            for (auto &o : sub)
                sas_operators.push_back(move(o));
        }
        return 0;
    });
    // Build SAS axioms from the simplified axiom list.
    vector<SASAxiom> sas_axioms;
    for (const auto &ax : axiom_layering.axioms) {
        if (!ax)
            continue;
        auto sub = translate_strips_axiom(
            *ax, strips_to_sas.factvals, strips_to_sas.ranges,
            mutex_dict.factvals, mutex_dict.ranges, fluent_ids);
        for (auto &a : sub)
            sas_axioms.push_back(move(a));
    }
    // Variables.
    SASVariables sas_vars;
    sas_vars.ranges = strips_to_sas.ranges;
    sas_vars.axiom_layers =
        build_axiom_layers(strips_to_sas, axiom_layering, fluent_ids);
    sas_vars.value_names = groups.translation_key;

    vector<SASMutexGroup> sas_mutexes =
        build_sas_mutexes(groups, strips_to_sas, fluent_ids, use_partial);

    sort_operators_canonically(sas_operators);

    SASTask sas_task;
    sas_task.variables = move(sas_vars);
    sas_task.mutexes = move(sas_mutexes);
    sas_task.init = move(sas_init);
    sas_task.goal = move(sas_goal);
    sas_task.operators = move(sas_operators);
    sas_task.axioms = move(sas_axioms);
    sas_task.metric = task.use_min_cost_metric;

    if (get_options().filter_unreachable_facts) {
        cout << "Detecting unreachable propositions..." << endl;
        utils::PhaseTimer simplify_t;
        try {
            simplify::filter_unreachable_propositions(sas_task);
        } catch (const simplify::Impossible &) {
            cout << "Simplified to trivially false goal!" << endl;
            return trivial_task(false);
        } catch (const simplify::TriviallySolvable &) {
            cout << "Simplified to empty goal!" << endl;
            return trivial_task(true);
        }
        cout << "Detecting unreachable propositions: " << simplify_t.str()
             << endl;
    }
    if (get_options().reorder_variables ||
        get_options().filter_unimportant_vars) {
        cout << "Reordering and filtering variables..." << endl;
        utils::PhaseTimer vo_t;
        simplify::find_and_apply_variable_order(
            sas_task, get_options().reorder_variables,
            get_options().filter_unimportant_vars);
        cout << "Reordering and filtering variables: " << vo_t.str() << endl;
    }
    // Axioms are emitted in canonical (condition, effect) order by
    // SASTask::output (post-remap), matching the Python translator's final
    // axiom sort.
    return sas_task;
}
}
