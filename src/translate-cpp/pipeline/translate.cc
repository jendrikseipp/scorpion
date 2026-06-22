#include "translate.h"

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
#include "../translate_options.h"
#include "../utils/timer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace translate::pipeline {
using namespace pddl;
using sas::SASAxiom;
using sas::SASGoal;
using sas::SASInit;
using sas::SASMutexGroup;
using sas::SASOperator;
using sas::SASTask;
using sas::SASVariables;
using sas::VarVal;

namespace {
std::string atom_key(const Atom &atom) {
    std::string k = atom.predicate;
    for (const auto &a : atom.args) { k.push_back('\x1f'); k += a; }
    return k;
}

// Same key as atom_key(), but written into a reused thread-local buffer and
// returned by reference, so the hot per-literal dict lookups in the
// "Translating task" phase don't heap-allocate a key string each time. The
// returned reference is only valid until the next call; callers use it
// immediately for a single find(). Negation does not affect the key, so this
// also lets negated-literal lookups skip building a temporary positive Atom.
const std::string &atom_key_scratch(const std::string &predicate,
                                    const std::vector<std::string> &args) {
    static thread_local std::string buf;
    buf.assign(predicate);
    for (const auto &a : args) { buf.push_back('\x1f'); buf += a; }
    return buf;
}

using AtomToVarVals =
    std::unordered_map<std::string, std::vector<VarVal>>;

struct StripsToSas {
    std::vector<int> ranges;
    AtomToVarVals dict;
};

StripsToSas build_dictionary(
    const std::vector<std::vector<ConditionPtr>> &groups,
    bool assert_partial) {
    StripsToSas out;
    out.ranges.reserve(groups.size());
    for (std::size_t var = 0; var < groups.size(); ++var) {
        for (std::size_t val = 0; val < groups[var].size(); ++val) {
            const auto &atom = static_cast<const Atom &>(*groups[var][val]);
            out.dict[atom_key(atom)].push_back({
                static_cast<int>(var), static_cast<int>(val)});
        }
        out.ranges.push_back(static_cast<int>(groups[var].size()) + 1);
    }
    if (assert_partial) {
        for (const auto &[_, v] : out.dict) {
            if (v.size() != 1)
                throw std::runtime_error(
                    "use-partial-encoding: atom must be in at most one group");
        }
    }
    return out;
}

// Facts (FDR pairs) implied by a fact: in every state containing p, all pairs
// in implied_facts[p] must also hold. Used only with --add-implied-preconditions.
using ImpliedFacts = std::map<VarVal, std::vector<VarVal>>;

/*
  Port of Python's build_implied_facts (main.py). The only exploited case is:
  p encodes a STRIPS proposition X, q encodes "not Y", and X and Y are mutex.
  For q to encode "not Y", Y must form a fact group of size 1 ("lonely"); then
  every other fact in Y's mutex group implies "not Y" = (Y's var, 1).
*/
ImpliedFacts build_implied_facts(const fact_groups::ComputedGroups &groups,
                                 const StripsToSas &strips_to_sas) {
    // Lonely propositions: size-1 fact groups -> their SAS variable number
    // (the proposition is encoded as (var, 0); see build_dictionary).
    std::unordered_map<std::string, int> lonely;
    for (std::size_t var = 0; var < groups.groups.size(); ++var) {
        if (groups.groups[var].size() == 1) {
            const auto &prop = static_cast<const Atom &>(*groups.groups[var][0]);
            lonely[atom_key(prop)] = static_cast<int>(var);
        }
    }
    ImpliedFacts implied;
    for (const auto &mutex_group : groups.mutex_groups) {
        for (std::size_t i = 0; i < mutex_group.size(); ++i) {
            const auto &prop = static_cast<const Atom &>(*mutex_group[i]);
            auto lit = lonely.find(atom_key(prop));
            if (lit == lonely.end()) continue;
            VarVal prop_is_false{lit->second, 1};
            for (std::size_t j = 0; j < mutex_group.size(); ++j) {
                if (j == i) continue;
                const auto &other = static_cast<const Atom &>(*mutex_group[j]);
                auto dit = strips_to_sas.dict.find(atom_key(other));
                if (dit == strips_to_sas.dict.end()) continue;
                for (const auto &fact : dit->second)
                    implied[fact].push_back(prop_is_false);
            }
        }
    }
    return implied;
}

// Map var -> set of allowed values (for a condition under construction).
using CondMap = std::unordered_map<int, std::set<int>>;

std::optional<std::vector<std::unordered_map<int, int>>>
translate_strips_conditions_aux(
    const std::vector<ConditionPtr> &conditions,
    const AtomToVarVals &dict, const std::vector<int> &ranges) {
    CondMap condition;
    // Positive literals first.
    for (const auto &c : conditions) {
        if (!c) continue;
        const auto &lit = static_cast<const Literal &>(*c);
        if (lit.negated()) continue;
        auto it = dict.find(atom_key_scratch(lit.predicate, lit.args));
        if (it == dict.end()) continue; // static
        for (const auto &[var, val] : it->second) {
            auto cit = condition.find(var);
            if (cit != condition.end()) {
                if (!cit->second.contains(val)) return std::nullopt;
                cit->second = {val};
            } else {
                condition[var] = {val};
            }
        }
    }
    // Negative literals.
    for (const auto &c : conditions) {
        if (!c) continue;
        const auto &lit = static_cast<const Literal &>(*c);
        if (!lit.negated()) continue;
        auto it = dict.find(atom_key_scratch(lit.predicate, lit.args));
        if (it == dict.end()) continue;
        bool done = false;
        CondMap new_condition;
        for (const auto &[var, val] : it->second) {
            std::set<int> poss_vals;
            for (int v = 0; v < ranges[var]; ++v)
                if (v != val) poss_vals.insert(v);
            auto cit = condition.find(var);
            if (cit == condition.end()) {
                new_condition[var] = std::move(poss_vals);
            } else {
                done = true;
                std::set<int> intersection;
                for (int v : cit->second)
                    if (poss_vals.contains(v)) intersection.insert(v);
                if (intersection.empty()) return std::nullopt;
                cit->second = std::move(intersection);
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
            std::size_t best_size = SIZE_MAX;
            for (const auto &[var, val] : it->second) {
                auto nit = new_condition.find(var);
                if (nit != new_condition.end() && nit->second.size() < best_size) {
                    best_size = nit->second.size();
                    best_var = var;
                }
            }
            condition[best_var] = std::move(new_condition[best_var]);
        }
    }
    // Multiply-out the condition.
    std::vector<std::pair<int, std::set<int>>> sorted_conds(
        condition.begin(), condition.end());
    std::ranges::sort(sorted_conds,
              [](const auto &a, const auto &b) {
                  return a.second.size() < b.second.size();
              });
    std::vector<std::unordered_map<int, int>> flat_conds = {{}};
    for (const auto &[var, vals] : sorted_conds) {
        if (vals.size() == 1) {
            int val = *vals.begin();
            for (auto &cond : flat_conds) cond[var] = val;
        } else {
            std::vector<std::unordered_map<int, int>> new_conds;
            for (const auto &cond : flat_conds) {
                for (int val : vals) {
                    auto nc = cond;
                    nc[var] = val;
                    new_conds.push_back(std::move(nc));
                }
            }
            flat_conds = std::move(new_conds);
        }
    }
    return flat_conds;
}

std::optional<std::vector<std::unordered_map<int, int>>>
translate_strips_conditions(
    const std::vector<ConditionPtr> &conditions,
    const AtomToVarVals &dict, const std::vector<int> &ranges,
    const AtomToVarVals &mutex_dict,
    const std::vector<int> &mutex_ranges) {
    if (conditions.empty()) return std::vector<std::unordered_map<int, int>>{{}};
    auto mtx = translate_strips_conditions_aux(conditions, mutex_dict,
                                               mutex_ranges);
    if (!mtx) return std::nullopt;
    return translate_strips_conditions_aux(conditions, dict, ranges);
}

std::optional<std::vector<std::unordered_map<int, int>>>
negate_and_translate_condition(
    const std::vector<std::vector<ConditionPtr>> &condition,
    const AtomToVarVals &dict, const std::vector<int> &ranges,
    const AtomToVarVals &mutex_dict,
    const std::vector<int> &mutex_ranges) {
    std::vector<std::unordered_map<int, int>> negation;
    // An empty group inside `condition` means "always satisfied" — the
    // negation is unsatisfiable. (Matches Python's `if [] in condition`.)
    for (const auto &group : condition)
        if (group.empty()) return std::nullopt;
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
    std::vector<std::size_t> idx(condition.size(), 0);
    while (true) {
        std::vector<ConditionPtr> combination;
        for (std::size_t i = 0; i < condition.size(); ++i)
            combination.push_back(condition[i][idx[i]]->negate());
        auto cond = translate_strips_conditions(combination, dict, ranges,
                                                mutex_dict, mutex_ranges);
        if (cond) for (auto &c : *cond) negation.push_back(std::move(c));
        // Increment.
        std::size_t k = condition.size();
        while (k > 0) {
            --k;
            if (++idx[k] < condition[k].size()) break;
            idx[k] = 0;
            if (k == 0) return negation.empty() ? std::nullopt
                                                : std::make_optional(negation);
        }
        if (k == 0 && idx[0] == 0) break;
    }
    return negation.empty() ? std::nullopt : std::make_optional(negation);
}

std::optional<SASOperator> build_sas_operator(
    const std::string &name,
    std::unordered_map<int, int> condition,
    std::map<int, std::map<int, std::vector<std::unordered_map<int, int>>>>
        &effects_by_variable,
    int cost, const std::vector<int> &ranges,
    const ImpliedFacts &implied_facts) {
    std::unordered_map<int, int> prevail_and_pre = condition;
    // Facts implied by the operator's (prevail + pre) condition. Computed from
    // the full condition before the effects loop erases entries from it.
    std::set<VarVal> implied_precondition;
    if (get_options().add_implied_preconditions) {
        for (const auto &[var, val] : condition) {
            auto it = implied_facts.find(VarVal{var, val});
            if (it != implied_facts.end())
                implied_precondition.insert(it->second.begin(),
                                            it->second.end());
        }
    }
    std::vector<std::tuple<int, int, int, std::vector<VarVal>>> pre_post;
    for (auto &[var, effects_on_var] : effects_by_variable) {
        int orig_pre = -1;
        auto cit = condition.find(var);
        if (cit != condition.end()) orig_pre = cit->second;
        bool added = false;
        for (auto &[post, eff_conds] : effects_on_var) {
            int pre = orig_pre;
            if (pre == post) continue;
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
            if (ranges[var] == 2 &&
                !effects_on_var.contains(1 - post)) {
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
                std::vector<VarVal> filtered;
                bool contradict = false;
                for (const auto &[cv, cval] : eff_cond) {
                    auto pit = prevail_and_pre.find(cv);
                    if (pit != prevail_and_pre.end()) {
                        if (pit->second != cval) {
                            contradict = true; break;
                        }
                    } else {
                        filtered.emplace_back(cv, cval);
                    }
                }
                if (contradict) continue;
                std::ranges::sort(filtered);
                pre_post.emplace_back(var, pre, post, std::move(filtered));
                added = true;
            }
        }
        if (added) condition.erase(var);
    }
    if (pre_post.empty() && !get_options().keep_no_ops) return std::nullopt;
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
    std::ranges::sort(pre_post);
    pre_post.erase(std::unique(pre_post.begin(), pre_post.end()),
                   pre_post.end());
    SASOperator op;
    op.name = name;
    for (const auto &[v, val] : condition) op.prevail.emplace_back(v, val);
    std::ranges::sort(op.prevail);
    op.pre_post = std::move(pre_post);
    op.cost = cost;
    return op;
}

std::optional<SASOperator> translate_strips_operator_aux(
    const PropositionalAction &op, const AtomToVarVals &dict,
    const std::vector<int> &ranges, const AtomToVarVals &mutex_dict,
    const std::vector<int> &mutex_ranges,
    const std::unordered_map<int, int> &condition,
    const ImpliedFacts &implied_facts) {
    std::map<int, std::map<int, std::vector<std::unordered_map<int, int>>>>
        effects_by_variable;
    std::map<int, std::vector<std::vector<ConditionPtr>>> add_conds_by_var;

    for (const auto &[conds, fact] : op.add_effects) {
        if (!fact) continue;
        auto eff_cond_list =
            translate_strips_conditions(conds, dict, ranges, mutex_dict,
                                        mutex_ranges);
        if (!eff_cond_list) continue;
        const auto &flit = static_cast<const Literal &>(*fact);
        auto it = dict.find(atom_key_scratch(flit.predicate, flit.args));
        if (it == dict.end()) continue;
        for (const auto &[var, val] : it->second) {
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
    using CondPtr = std::shared_ptr<std::unordered_map<int, int>>;
    std::vector<int> del_var_order;
    std::unordered_map<int, std::vector<std::pair<int, CondPtr>>> del_by_var;
    for (const auto &[conds, fact] : op.del_effects) {
        if (!fact) continue;
        auto eff_cond_list =
            translate_strips_conditions(conds, dict, ranges, mutex_dict,
                                        mutex_ranges);
        if (!eff_cond_list) continue;
        const auto &flit = static_cast<const Literal &>(*fact);
        auto it = dict.find(atom_key_scratch(flit.predicate, flit.args));
        if (it == dict.end()) continue;
        // One shared condition object per translated effect-condition, reused
        // across every representation of this deleted fact.
        std::vector<CondPtr> shared;
        shared.reserve(eff_cond_list->size());
        for (const auto &ec : *eff_cond_list)
            shared.push_back(std::make_shared<std::unordered_map<int, int>>(ec));
        for (const auto &[var, val] : it->second) {
            if (!del_by_var.contains(var)) del_var_order.push_back(var);
            for (const auto &sp : shared)
                del_by_var[var].emplace_back(val, sp);
        }
    }
    // For each del-effect var (in insertion order), add the var=none_of_those
    // effect guarded by "the deleted value held and no add effect triggers".
    for (int var : del_var_order) {
        auto no_add = negate_and_translate_condition(
            add_conds_by_var[var], dict, ranges, mutex_dict, mutex_ranges);
        if (!no_add) continue;
        int none_of_those = ranges[var] - 1;
        for (auto &[val, cond_ptr] : del_by_var[var]) {
            auto &cond = *cond_ptr;
            auto cit = cond.find(var);
            if (cit != cond.end() && cit->second != val) continue;
            cond[var] = val;  // mutate the shared condition (guards accumulate)
            for (const auto &no_add_cond : *no_add) {
                std::unordered_map<int, int> new_cond = cond;
                bool bad = false;
                for (const auto &[cv, cval] : no_add_cond) {
                    auto pit = new_cond.find(cv);
                    if (pit != new_cond.end() && pit->second != cval) {
                        bad = true; break;
                    }
                    new_cond[cv] = cval;
                }
                if (!bad)
                    effects_by_variable[var][none_of_those].push_back(
                        std::move(new_cond));
            }
        }
    }
    return build_sas_operator(op.name, condition, effects_by_variable,
                              op.cost, ranges, implied_facts);
}

std::vector<SASOperator> translate_strips_operator(
    const PropositionalAction &op, const AtomToVarVals &dict,
    const std::vector<int> &ranges, const AtomToVarVals &mutex_dict,
    const std::vector<int> &mutex_ranges,
    const ImpliedFacts &implied_facts) {
    std::vector<SASOperator> result;
    auto conds = translate_strips_conditions(op.precondition, dict, ranges,
                                             mutex_dict, mutex_ranges);
    if (!conds) return result;
    for (const auto &c : *conds) {
        auto op_out = translate_strips_operator_aux(op, dict, ranges,
                                                    mutex_dict, mutex_ranges,
                                                    c, implied_facts);
        if (op_out) result.push_back(std::move(*op_out));
    }
    return result;
}

std::vector<SASAxiom> translate_strips_axiom(
    const PropositionalAxiom &ax, const AtomToVarVals &dict,
    const std::vector<int> &ranges, const AtomToVarVals &mutex_dict,
    const std::vector<int> &mutex_ranges) {
    std::vector<SASAxiom> out;
    auto conds = translate_strips_conditions(ax.condition, dict, ranges,
                                             mutex_dict, mutex_ranges);
    if (!conds) return out;
    if (!ax.effect) return out;
    auto it = dict.find(atom_key(*ax.effect));
    if (it == dict.end() || it->second.size() != 1) return out;
    VarVal eff = it->second.front();
    for (const auto &c : *conds) {
        SASAxiom sa;
        for (const auto &[v, val] : c) sa.condition.emplace_back(v, val);
        std::ranges::sort(sa.condition);
        sa.effect = eff;
        out.push_back(std::move(sa));
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
}

SASTask pddl_to_sas(Task &task) {
    // Label each phase with the Python translator's wording and print
    // the "[%.3fs CPU, %.3fs wall-clock]" suffix so Lab's stock
    // translator parser captures them as translator_time_<label>.
    auto phase = [](const char *label, auto fn) {
        utils::PhaseTimer t;
        auto v = fn();
        std::cout << label << ": " << t.str() << std::endl;
        return v;
    };
    std::cout << "Instantiating..." << std::endl;
    auto prog = phase("Generating Datalog program",
                      [&] { return grounding::build_program(task); });
    phase("Normalizing Datalog program",
          [&] { grounding::split_rules(prog); return 0; });
    auto model = phase("Computing model",
                       [&] { return grounding::compute_model(prog); });
    auto inst = phase("Completing instantiation",
                      [&] { return instantiate::instantiate(task, model); });

    if (!inst.relaxed_reachable) {
        std::cout << "No relaxed solution! Generating unsolvable task..."
                  << std::endl;
        return trivial_task(false);
    }
    if (!inst.instantiated_goal) {
        std::cout << "Trivially false goal! Generating unsolvable task..."
                  << std::endl;
        return trivial_task(false);
    }
    AtomSet negative_in_goal;
    for (const auto &g : *inst.instantiated_goal) {
        if (!g) continue;
        const auto &lit = static_cast<const Literal &>(*g);
        if (lit.negated()) {
            negative_in_goal.insert(std::make_shared<const Atom>(
                lit.predicate, lit.args));
        }
    }

    std::cout << "Computing fact groups..." << std::endl;
    auto groups = phase("Computing fact groups", [&] {
        return fact_groups::compute_groups(
            task, inst.fluent_facts, &inst.reachable_action_parameters,
            negative_in_goal);
    });

    bool use_partial = get_options().use_partial_encoding;
    auto strips_to_sas = build_dictionary(groups.groups, use_partial);
    auto mutex_dict = build_dictionary(groups.mutex_groups, false);

    // Facts implied by other facts (only used by --add-implied-preconditions).
    ImpliedFacts implied_facts;
    if (get_options().add_implied_preconditions)
        implied_facts = build_implied_facts(groups, strips_to_sas);


    // Build init.
    SASInit sas_init;
    sas_init.values.assign(strips_to_sas.ranges.size(), 0);
    for (std::size_t v = 0; v < strips_to_sas.ranges.size(); ++v)
        sas_init.values[v] = strips_to_sas.ranges[v] - 1;
    for (const auto &elem : task.init) {
        if (!std::holds_alternative<std::shared_ptr<const Atom>>(elem))
            continue;
        const auto &ap = std::get<std::shared_ptr<const Atom>>(elem);
        if (!ap) continue;
        auto it = strips_to_sas.dict.find(atom_key(*ap));
        if (it == strips_to_sas.dict.end()) continue;
        for (const auto &[var, val] : it->second)
            sas_init.values[var] = val;
    }
    // Build goal.
    auto goal_conds = translate_strips_conditions(
        *inst.instantiated_goal, strips_to_sas.dict, strips_to_sas.ranges,
        mutex_dict.dict, mutex_dict.ranges);
    if (!goal_conds) {
        std::cout << "Goal violates a mutex! Generating unsolvable task..."
                  << std::endl;
        return trivial_task(false);
    }
    if (goal_conds->size() != 1)
        throw std::runtime_error("Negative goal not supported");
    SASGoal sas_goal;
    for (const auto &[v, val] : goal_conds->front())
        sas_goal.pairs.emplace_back(v, val);
    std::ranges::sort(sas_goal.pairs);
    if (sas_goal.pairs.empty()) {
        std::cout << "Empty goal! Generating solvable task..." << std::endl;
        return trivial_task(true);
    }

    // Process axioms and compute axiom layers.
    auto axiom_layering = phase("Processing axioms", [&] {
        return axioms::handle_axioms(
            inst.instantiated_actions, inst.instantiated_axioms,
            *inst.instantiated_goal, get_options().layer_strategy);
    });

    // Build operators.
    std::vector<SASOperator> sas_operators;
    phase("Translating task", [&] {
        for (const auto &op : inst.instantiated_actions) {
            if (!op) continue;
            auto sub = translate_strips_operator(
                *op, strips_to_sas.dict, strips_to_sas.ranges,
                mutex_dict.dict, mutex_dict.ranges, implied_facts);
            for (auto &o : sub) sas_operators.push_back(std::move(o));
        }
        return 0;
    });
    // Build SAS axioms from the simplified axiom list.
    std::vector<SASAxiom> sas_axioms;
    for (const auto &ax : axiom_layering.axioms) {
        if (!ax) continue;
        auto sub = translate_strips_axiom(*ax, strips_to_sas.dict,
                                          strips_to_sas.ranges,
                                          mutex_dict.dict,
                                          mutex_dict.ranges);
        for (auto &a : sub) sas_axioms.push_back(std::move(a));
    }
    // Build axiom layers vector.
    std::vector<int> axiom_layers(strips_to_sas.ranges.size(), -1);
    for (const auto &[key, layer] : axiom_layering.axiom_layers) {
        auto it = strips_to_sas.dict.find(key);
        if (it == strips_to_sas.dict.end() || it->second.empty()) continue;
        axiom_layers[it->second.front().first] = layer;
    }

    // Variables.
    SASVariables sas_vars;
    sas_vars.ranges = strips_to_sas.ranges;
    sas_vars.axiom_layers = std::move(axiom_layers);
    sas_vars.value_names = groups.translation_key;

    // Mutex key: groups represented in strips_to_sas dict.
    std::vector<SASMutexGroup> sas_mutexes;
    if (use_partial) {
        for (const auto &grp : groups.mutex_groups) {
            std::vector<VarVal> facts;
            for (const auto &f : grp) {
                if (!f) continue;
                auto it = strips_to_sas.dict.find(
                    atom_key(static_cast<const Atom &>(*f)));
                if (it == strips_to_sas.dict.end() || it->second.size() != 1)
                    continue;
                facts.push_back(it->second.front());
            }
            if (facts.size() >= 2) sas_mutexes.emplace_back(std::move(facts));
        }
    }

    // Sort operators by (name, prevail, pre_post) at SAS construction
    // time -- before simplify and variable_order touch the task. That
    // matches Python's SASTask.__init__ ordering exactly. variable_order's
    // remap then renames var numbers without resorting, so the final
    // operator order in the output reflects the pre-remap canonical sort
    // rather than a post-remap one (which is what SASOperator::output
    // used to do).
    std::ranges::sort(sas_operators,
              [](const SASOperator &a, const SASOperator &b) {
                  if (a.name != b.name) return a.name < b.name;
                  if (a.prevail != b.prevail) return a.prevail < b.prevail;
                  return a.pre_post < b.pre_post;
              });
    SASTask sas_task;
    sas_task.variables = std::move(sas_vars);
    sas_task.mutexes = std::move(sas_mutexes);
    sas_task.init = std::move(sas_init);
    sas_task.goal = std::move(sas_goal);
    sas_task.operators = std::move(sas_operators);
    sas_task.axioms = std::move(sas_axioms);
    sas_task.metric = task.use_min_cost_metric;

    if (get_options().filter_unreachable_facts) {
        std::cout << "Detecting unreachable propositions..." << std::endl;
        utils::PhaseTimer simplify_t;
        try {
            simplify::filter_unreachable_propositions(sas_task);
        } catch (const simplify::Impossible &) {
            std::cout << "Simplified to trivially false goal!" << std::endl;
            return trivial_task(false);
        } catch (const simplify::TriviallySolvable &) {
            std::cout << "Simplified to empty goal!" << std::endl;
            return trivial_task(true);
        }
        std::cout << "Detecting unreachable propositions: " << simplify_t.str()
                  << std::endl;
    }
    if (get_options().reorder_variables ||
        get_options().filter_unimportant_vars) {
        std::cout << "Reordering and filtering variables..." << std::endl;
        utils::PhaseTimer vo_t;
        simplify::find_and_apply_variable_order(
            sas_task, get_options().reorder_variables,
            get_options().filter_unimportant_vars);
        std::cout << "Reordering and filtering variables: " << vo_t.str()
                  << std::endl;
    }
    // Axioms are emitted in canonical (condition, effect) order by
    // SASTask::output (post-remap), matching the Python translator's final
    // axiom sort.
    return sas_task;
}
}
