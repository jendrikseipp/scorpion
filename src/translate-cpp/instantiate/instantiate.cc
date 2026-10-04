#include "instantiate.h"

#include "../translate_options.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/f_expression.h"
#include "../pddl/task.h"
#include "../utils/hash.h"
#include "../utils/system.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

using namespace std;
namespace translate::instantiate {
using namespace pddl;

namespace {
// Interned ids of the predicates that can appear as (action/axiom) effects.
// build_fluent_facts uses these to select the fluent atoms from the model with
// an int lookup instead of hashing each atom's predicate name (equal names
// always intern to the same id).
unordered_set<int> get_fluent_predicates(const Task &task) {
    unordered_set<int> out;
    for (const auto &a : task.actions) {
        for (const auto &eff : a.effects) {
            if (eff.literal) {
                const auto &lit = static_cast<const Literal &>(*eff.literal);
                out.insert(grounding::symbols().intern(lit.predicate));
            }
        }
    }
    for (const auto &x : task.axioms)
        out.insert(grounding::symbols().intern(x.name));
    return out;
}

// Two views of the reachable fluent facts: the GroundKey->FactId lookup and
// the FactId->Atom vector, also used by fact-group expansion. The model has
// unique atoms, so each fact receives one dense FactId in model order.
struct FluentFacts {
    FactMap ids; // GroundKey -> FactId
    vector<shared_ptr<const Atom>> fact_by_id; // FactId -> Atom
};

FluentFacts build_fluent_facts(
    const vector<grounding::Atom> &model,
    const unordered_set<int> &fluent_preds) {
    FluentFacts out;
    for (const auto &a : model) {
        if (!fluent_preds.contains(a.predicate))
            continue;
        GroundKey key;
        key.predicate = a.predicate;
        for (const auto &x : a.args)
            key.args.push_back(x.v);
        // The model has no duplicate atoms, so assign the next FactId.
        FactId id = static_cast<FactId>(out.fact_by_id.size());
        vector<string> args;
        args.reserve(a.args.size());
        for (const auto &x : a.args)
            args.push_back(grounding::arg_to_string(x));
        auto atom = make_shared<const Atom>(a.predicate_name(), move(args));
        out.ids.insert(move(key), id);
        out.fact_by_id.push_back(move(atom));
    }
    return out;
}

// Rebuild a ground literal as an Atom/NegatedAtom (for the few axiom and goal
// literals, which stay ConditionPtr-based downstream).
ConditionPtr to_condition(
    const GroundLiteral &lit,
    const vector<shared_ptr<const Atom>> &fact_by_id) {
    const auto &atom = fact_by_id[lit.fact];
    if (!lit.negated)
        return atom;
    return make_shared<NegatedAtom>(atom->predicate, atom->args);
}

// Add static-true init facts to the fact map: each init atom whose ground key
// is not already a reachable fluent fact is a static fact, marked STATIC_FACT.
// (Fluent facts were inserted with their FactId first and take priority.)
void add_static_init_facts(const Task &task, FactMap &facts) {
    for (const auto &elem : task.init) {
        auto *ap = get_if<shared_ptr<const Atom>>(&elem);
        if (!ap || !*ap)
            continue;
        GroundKey key;
        key.predicate = grounding::symbols().intern((*ap)->predicate);
        for (const auto &arg : (*ap)->args)
            key.args.push_back(grounding::symbols().intern(arg));
        facts.insert(move(key), STATIC_FACT);
    }
}

// Identity of a primitive numeric expression: its function symbol and argument
// names. A pair keyed map replaces the old symbol + '\x1f' + args string key --
// no separator-byte convention, and the cost lookup no longer rebuilds a joined
// string per ground action.
using PneKey = pair<string, vector<string>>;
struct PneKeyHash {
    size_t operator()(const PneKey &k) const noexcept {
        size_t h = hash<string>{}(k.first);
        for (const auto &a : k.second)
            utils::hash_combine(h, hash<string>{}(a));
        return h;
    }
};
using InitAssignments =
    unordered_map<PneKey, shared_ptr<const FunctionalExpression>, PneKeyHash>;

// PNE-to-expression map for init assignments.
InitAssignments build_init_assignments(const Task &task) {
    InitAssignments out;
    for (const auto &elem : task.init) {
        if (auto *as = get_if<shared_ptr<Assign>>(&elem)) {
            if (*as && (*as)->fluent)
                out[{(*as)->fluent->symbol, (*as)->fluent->args}] =
                    (*as)->expression;
        }
    }
    return out;
}

// Objects grouped by (super)type, as interned object ids -- so binding a
// parameter during instantiation stores an id directly, feeding the integer
// ground-fact probe without any string work.
unordered_map<string, vector<int>> get_objects_by_type(const Task &task) {
    unordered_map<string, vector<int>> result;
    unordered_map<string, vector<string>> supertypes;
    for (const auto &t : task.types)
        supertypes[t.name] = t.supertype_names;
    for (const auto &obj : task.objects) {
        int id = grounding::symbols().intern(obj.name);
        result[obj.type_name].push_back(id);
        for (const auto &sup : supertypes[obj.type_name])
            result[sup].push_back(id);
    }
    return result;
}

// Restrict one quantified parameter using its first static binary/ternary guard.
// Relations are shared across types. Unsorted typed pools use the old path.
// Plans borrow task/type storage, which stays unchanged during instantiate().
// Retained assignments still evaluate the full condition in its original order.
class StaticGuardDomains {
    struct FixedPairHash {
        size_t operator()(const array<int, 2> &key) const noexcept {
            size_t h = hash<int>{}(key[0]);
            utils::hash_combine(h, hash<int>{}(key[1]));
            return h;
        }
    };
    struct Plan {
        const string *fixed_arg;
        const string *second_fixed_arg; // nullptr for a binary guard
        const vector<int> *objects;
        size_t relation;
    };
    vector<unordered_map<int, vector<int>>> relations_;
    vector<unordered_map<array<int, 2>, vector<int>, FixedPairHash>>
        ternary_relations_;
    unordered_map<const Effect *, Plan> plans_;
    const vector<int> empty_;

public:
    struct Domain {
        const vector<int> &objects;
        const vector<int> &matches;
    };

    StaticGuardDomains(
        const Task &task, const unordered_set<int> &fluent_preds,
        const FactMap &facts,
        const unordered_map<string, vector<int>> &objects_by_type) {
        // Separate mode namespaces preserve arity even for internal Tasks.
        array<map<pair<int, int>, size_t>, 2> modes;
        vector<const vector<int> *> ternary_pools;
        unordered_map<const vector<int> *, bool> sorted_types;
        for (const auto &action : task.actions) {
            for (const auto &eff : action.effects) {
                if (eff.parameters.size() != 1 || !eff.condition)
                    continue;
                const Condition *first = eff.condition.get();
                if (first->kind() == Condition::Kind::CONJUNCTION) {
                    const auto &parts = first->parts();
                    first = parts.empty() ? nullptr : parts.front().get();
                }
                if (!first || first->kind() != Condition::Kind::ATOM)
                    continue;
                const auto &guard = static_cast<const Literal &>(*first);
                size_t arity = guard.args.size();
                if ((arity != 2 && arity != 3) ||
                    fluent_preds.contains(guard.predicate_id))
                    continue;
                int quantified_arg = -1;
                size_t occurrences = 0;
                for (size_t i = 0; i < arity; ++i)
                    if (guard.args[i] == eff.parameters[0].name) {
                        quantified_arg = static_cast<int>(i);
                        ++occurrences;
                    }
                if (occurrences != 1)
                    continue;
                auto objects = objects_by_type.find(eff.parameters[0].type_name);
                if (objects == objects_by_type.end() || objects->second.empty())
                    continue;
                // No nonempty row can pass the query bound for this pool.
                size_t pool_size = objects->second.size();
                if (pool_size / (2 * bit_width(pool_size)) == 0)
                    continue;
                auto [sorted, new_type] = sorted_types.try_emplace(
                    &objects->second, false);
                if (new_type)
                    sorted->second = ranges::is_sorted(objects->second);
                if (!sorted->second)
                    continue;
                auto [mode, new_mode] = modes[arity - 2].try_emplace(
                    pair{guard.predicate_id, quantified_arg},
                    arity == 2 ? relations_.size() : ternary_relations_.size());
                if (new_mode) {
                    if (arity == 2)
                        relations_.emplace_back();
                    else {
                        ternary_relations_.emplace_back();
                        ternary_pools.push_back(&objects->second);
                    }
                } else if (arity == 3 &&
                           ternary_pools[mode->second] != &objects->second) {
                    // A shared ternary mode needs one exact typed universe.
                    ternary_pools[mode->second] = nullptr;
                }
                array<const string *, 2> fixed_args{};
                size_t fixed_position = 0;
                for (size_t i = 0; i < arity; ++i)
                    if (static_cast<int>(i) != quantified_arg)
                        fixed_args[fixed_position++] = &guard.args[i];
                plans_.emplace(&eff, Plan{
                    fixed_args[0], fixed_args[1], &objects->second, mode->second});
            }
        }
        // Mixed typed pools use the original evaluator; do not build their rows.
        erase_if(plans_, [&](const auto &entry) {
            const Plan &plan = entry.second;
            return plan.second_fixed_arg && !ternary_pools[plan.relation];
        });
        erase_if(modes[1], [&](const auto &entry) {
            return !ternary_pools[entry.second];
        });
        if (modes[0].empty() && modes[1].empty())
            return;
        for (const auto &element : task.init) {
            const auto *atom = get_if<shared_ptr<const Atom>>(&element);
            if (!atom || !*atom)
                continue;
            size_t arity = (*atom)->args.size();
            if (arity != 2 && arity != 3)
                continue;
            int predicate = (*atom)->predicate_id;
            const auto &arity_modes = modes[arity - 2];
            if (arity_modes.empty())
                continue;
            array<optional<size_t>, 3> requested{};
            bool any_requested = false;
            for (size_t i = 0; i < arity; ++i) {
                auto mode = arity_modes.find({predicate, static_cast<int>(i)});
                if (mode != arity_modes.end()) {
                    requested[i] = mode->second;
                    any_requested = true;
                }
            }
            if (!any_requested)
                continue;
            GroundKey key;
            key.predicate = predicate;
            for (const auto &arg : (*atom)->args)
                key.args.push_back(grounding::symbols().intern(arg));
            const FactId *id = facts.find(key);
            if (!id || *id != STATIC_FACT)
                continue;
            for (size_t i = 0; i < arity; ++i) {
                if (!requested[i])
                    continue;
                if (arity == 2) {
                    relations_[*requested[i]][key.args[1 - i]].push_back(
                        key.args[i]);
                } else {
                    if (!ranges::binary_search(
                            *ternary_pools[*requested[i]], key.args[i]))
                        continue;
                    array<int, 2> fixed;
                    size_t fixed_position = 0;
                    for (size_t j = 0; j < arity; ++j)
                        if (j != i)
                            fixed[fixed_position++] = key.args[j];
                    ternary_relations_[*requested[i]][fixed].push_back(
                        key.args[i]);
                }
            }
        }
        for (auto &relation : relations_)
            for (auto &[fixed, values] : relation) {
                ranges::sort(values);
                values.erase(unique(values.begin(), values.end()), values.end());
            }
        for (auto &relation : ternary_relations_)
            for (auto &[fixed, values] : relation) {
                ranges::sort(values);
                values.erase(unique(values.begin(), values.end()), values.end());
            }
    }

    // nullopt means unsupported; an empty match list means no assignment.
    optional<Domain> restrict_assignments(
        const Effect &eff, const VarMapping &mapping) const {
        auto plan = plans_.find(&eff);
        if (plan == plans_.end())
            return nullopt;
        const string &arg = *plan->second.fixed_arg;
        auto binding = mapping.find(arg);
        if (binding == mapping.end() && !arg.empty() && arg[0] == '?')
            return nullopt;
        int fixed = binding == mapping.end()
                        ? grounding::symbols().intern(arg) : binding->second;
        const vector<int> *matches = &empty_;
        if (plan->second.second_fixed_arg) {
            const string &second_arg = *plan->second.second_fixed_arg;
            auto second_binding = mapping.find(second_arg);
            if (second_binding == mapping.end() &&
                !second_arg.empty() && second_arg[0] == '?')
                return nullopt;
            int second_fixed = second_binding == mapping.end()
                                   ? grounding::symbols().intern(second_arg)
                                   : second_binding->second;
            const auto &relation = ternary_relations_[plan->second.relation];
            auto row = relation.find(array<int, 2>{fixed, second_fixed});
            if (row != relation.end())
                matches = &row->second;
        } else {
            const auto &relation = relations_[plan->second.relation];
            auto row = relation.find(fixed);
            if (row != relation.end())
                matches = &row->second;
        }
        // Bound indexed searches by one scan of the typed pool; broad or dense
        // relation rows may otherwise cost more than the original enumeration.
        if (!matches->empty() &&
            matches->size() > plan->second.objects->size() /
                (2 * bit_width(plan->second.objects->size())))
            return nullopt;
        return Domain{*plan->second.objects, *matches};
    }
};

// Recursively iterate over the cartesian product of objects-by-type for
// each parameter, calling `fn(var_mapping)` for each assignment.
void for_each_assignment(
    const vector<TypedObject> &parameters, VarMapping &var_mapping,
    const unordered_map<string, vector<int>> &objects_by_type,
    const function<void()> &fn, size_t depth = 0) {
    if (depth == parameters.size()) {
        fn();
        return;
    }
    const auto &par = parameters[depth];
    auto it = objects_by_type.find(par.type_name);
    if (it == objects_by_type.end())
        return;
    for (const auto &obj : it->second) {
        var_mapping[par.name] = obj;
        for_each_assignment(
            parameters, var_mapping, objects_by_type, fn, depth + 1);
    }
}

void instantiate_effect(
    const Effect &eff, VarMapping &var_mapping, const FactMap &fluent_facts,
    const unordered_map<string, vector<int>> &objects_by_type,
    const StaticGuardDomains &guard_domains,
    vector<GroundEffect> &result) {
    auto inst_once = [&]() {
        GroundLiteralList condition;
        if (eff.condition &&
            !eff.condition->instantiate(var_mapping, fluent_facts, condition))
            return;
        GroundLiteralList lit_out;
        if (eff.literal &&
            !eff.literal->instantiate(var_mapping, fluent_facts, lit_out))
            return;
        if (!lit_out.empty()) {
            result.push_back({move(condition), move(lit_out[0])});
        }
    };
    if (eff.parameters.empty()) {
        inst_once();
    } else if (auto domain =
                   guard_domains.restrict_assignments(eff, var_mapping)) {
        for (int object : domain->matches) {
            auto [begin, end] = ranges::equal_range(domain->objects, object);
            for (auto pos = begin; pos != end; ++pos) {
                var_mapping[eff.parameters[0].name] = *pos;
                inst_once();
            }
        }
    } else {
        for_each_assignment(
            eff.parameters, var_mapping, objects_by_type, inst_once);
    }
}

long long evaluate_constant(const FunctionalExpression &expr) {
    if (expr.kind() == FunctionalExpression::Kind::CONSTANT) {
        return static_cast<const NumericConstant &>(expr).value;
    }
    throw runtime_error("cost expression is not a numeric constant");
}

// Ground the action's cost under `var_mapping`: 1 without a metric; otherwise
// evaluate its cost expression (a constant directly, or a PNE looked up in the
// initial assignments), or 0 if the action has no cost expression.
long long resolve_action_cost(
    const Action &action, const VarMapping &var_mapping,
    const InitAssignments &init_assignments, bool use_metric) {
    if (!use_metric)
        return 1;
    if (!action.cost || !action.cost->expression)
        return 0;
    // Instantiate the cost expression: if it's a PNE, look up in
    // init_assignments; else if it's a constant, use it.
    if (action.cost->expression->kind() != FunctionalExpression::Kind::PNE)
        return evaluate_constant(*action.cost->expression);
    const auto &pne = static_cast<const PrimitiveNumericExpression &>(
        *action.cost->expression);
    vector<string> resolved_args;
    resolved_args.reserve(pne.args.size());
    for (const auto &a : pne.args) {
        auto it = var_mapping.find(a);
        resolved_args.push_back(
            it == var_mapping.end() ? a
                                    : grounding::symbols().name(it->second));
    }
    auto it = init_assignments.find(PneKey{pne.symbol, move(resolved_args)});
    if (it == init_assignments.end())
        throw runtime_error("Could not find PNE initialization for cost");
    return evaluate_constant(*it->second);
}

optional<PropositionalAction> instantiate_action(
    const Action &action, const grounding::ArgList &args,
    const InitAssignments &init_assignments, const FactMap &fluent_facts,
    const unordered_map<string, vector<int>> &objects_by_type,
    const StaticGuardDomains &guard_domains,
    bool use_metric) {
    // Applicability heads may include additional existential bindings.
    if (args.size() < action.parameters.size())
        return nullopt;
    // Reused across ground actions (instantiate_action is never re-entrant):
    // clear() keeps the backing buffer, so rebinding per ground action neither
    // frees nor re-allocates in the dominant instantiation phase. Parameterised
    // effects still take their own copy before binding extra parameters.
    static thread_local VarMapping var_mapping;
    var_mapping.clear();
    for (size_t i = 0; i < action.parameters.size(); ++i)
        var_mapping[action.parameters[i].name] = args[i].v;

    // Build the grounded name using only external parameters.
    //
    // We mirror Python's `"(%s %s)" % (action.name, " ".join(args))`
    // exactly -- that format always emits a space after action.name,
    // even when the args list is empty, producing "(name )" (with a
    // space before the close paren). After SAS-output paren-stripping
    // this becomes a trailing-space in the operator name, which is
    // load-bearing for byte-identical output and stable sort key.
    string name = "(" + action.name + " ";
    for (int i = 0; i < action.num_external_parameters; ++i) {
        if (i > 0)
            name.push_back(' ');
        name += args[i].name();
    }
    name.push_back(')');

    GroundLiteralList precondition;
    if (action.precondition && !action.precondition->instantiate(
                                   var_mapping, fluent_facts, precondition))
        return nullopt;

    vector<GroundEffect> effects;
    for (const auto &eff : action.effects) {
        if (eff.parameters.empty()) {
            // A parameterless effect adds no bindings, and instantiate()
            // only reads var_mapping, so share the action's mapping
            // directly instead of copying the whole map per effect.
            instantiate_effect(
                eff, var_mapping, fluent_facts, objects_by_type,
                guard_domains, effects);
        } else {
            VarMapping local_mapping = var_mapping;
            instantiate_effect(
                eff, local_mapping, fluent_facts, objects_by_type,
                guard_domains, effects);
        }
    }
    if (!effects.empty() || get_options().keep_no_ops) {
        long long cost = resolve_action_cost(
            action, var_mapping, init_assignments, use_metric);
        return PropositionalAction(
            name, move(precondition), move(effects), static_cast<int>(cost));
    }
    return nullopt;
}

shared_ptr<PropositionalAxiom> instantiate_axiom(
    const Axiom &axiom, const vector<string> &args, const FactMap &fluent_facts,
    const vector<shared_ptr<const Atom>> &fact_by_id) {
    if (args.size() != axiom.parameters.size())
        return nullptr;
    VarMapping var_mapping;
    for (size_t i = 0; i < axiom.parameters.size(); ++i)
        var_mapping[axiom.parameters[i].name] =
            grounding::symbols().intern(args[i]);

    vector<string> name_args;
    name_args.push_back(axiom.name);
    for (int i = 0; i < axiom.num_external_parameters; ++i)
        name_args.push_back(args[i]);
    string name = "(";
    for (size_t i = 0; i < name_args.size(); ++i) {
        if (i)
            name.push_back(' ');
        name += name_args[i];
    }
    name.push_back(')');

    GroundLiteralList condition_lits;
    if (axiom.condition && !axiom.condition->instantiate(
                               var_mapping, fluent_facts, condition_lits))
        return nullptr;
    // Axioms are few: keep the downstream (axiom_rules) atom-based.
    vector<ConditionPtr> condition;
    condition.reserve(condition_lits.size());
    for (const auto &l : condition_lits)
        condition.push_back(to_condition(l, fact_by_id));

    GroundKey effect_key;
    bool all_effect_args_bound = true;
    vector<string> eff_args;
    eff_args.reserve(axiom.num_external_parameters);
    for (int i = 0; i < axiom.num_external_parameters; ++i) {
        const auto &n = axiom.parameters[i].name;
        auto it = var_mapping.find(n);
        eff_args.push_back(
            it == var_mapping.end() ? n
                                    : grounding::symbols().name(it->second));
        if (it == var_mapping.end())
            all_effect_args_bound = false;
        else
            effect_key.args.push_back(it->second);
    }
    // Positive condition literals already share these immutable fact atoms.
    // Preserve the fresh-Atom fallback for unmatched internal model inputs.
    effect_key.predicate = grounding::symbols().intern(axiom.name);
    shared_ptr<const Atom> effect;
    if (all_effect_args_bound) {
        const FactId *id = fluent_facts.find(effect_key);
        if (id && *id >= 0 && static_cast<size_t>(*id) < fact_by_id.size()) {
            const auto &canonical = fact_by_id[*id];
            if (canonical &&
                canonical->predicate_id == effect_key.predicate &&
                canonical->predicate == axiom.name &&
                canonical->args == eff_args &&
                canonical->hash() ==
                    pddl::detail::literal_hash(axiom.name, eff_args))
                effect = canonical;
        }
    }
    if (!effect)
        effect = make_shared<const Atom>(axiom.name, move(eff_args));
    return make_shared<PropositionalAxiom>(
        move(name), move(condition), move(effect));
}

optional<vector<ConditionPtr>> instantiate_goal(
    const ConditionPtr &goal, const FactMap &fluent_facts,
    const vector<shared_ptr<const Atom>> &fact_by_id) {
    GroundLiteralList lits;
    VarMapping empty;
    if (goal && !goal->instantiate(empty, fluent_facts, lits))
        return nullopt;
    vector<ConditionPtr> result;
    result.reserve(lits.size());
    for (const auto &l : lits)
        result.push_back(to_condition(l, fact_by_id));
    return result;
}
}

Result instantiate(
    const Task &task, const vector<grounding::Atom> &model,
    const grounding::PredicateRoles &roles) {
    Result out;
    out.parameter_equalities.resize(task.actions.size());
    for (size_t i = 0; i < task.actions.size(); ++i) {
        auto &pairs = out.parameter_equalities[i].always_distinct;
        size_t parameter_count = task.actions[i].parameters.size();
        for (size_t p1 = 0; p1 < parameter_count; ++p1)
            for (size_t p2 = p1 + 1; p2 < parameter_count; ++p2)
                pairs.emplace_back(static_cast<int>(p1), static_cast<int>(p2));
    }
    auto fluent_preds = get_fluent_predicates(task);
    auto fluent = build_fluent_facts(model, fluent_preds);
    out.fluent_fact_ids = move(fluent.ids);
    out.fact_by_id = move(fluent.fact_by_id);
    const FactMap &fluent_facts = out.fluent_fact_ids;
    const auto &fact_by_id = out.fact_by_id;
    add_static_init_facts(task, out.fluent_fact_ids);
    auto init_assignments = build_init_assignments(task);
    auto objects_by_type = get_objects_by_type(task);
    StaticGuardDomains guard_domains(
        task, fluent_preds, fluent_facts, objects_by_type);

    for (const auto &atom : model) {
        switch (roles.role_of(atom.predicate)) {
        case grounding::PredicateRole::GOAL_REACHABLE:
            out.relaxed_reachable = true;
            break;
        case grounding::PredicateRole::ACTION: {
            int action_idx = roles.index_of(atom.predicate);
            const Action &action = task.actions[action_idx];
            if (atom.args.size() < action.parameters.size())
                break;
            // A single equal assignment disproves a pair's inequality.
            // Record every reachable grounding, even if instantiation fails.
            auto &pairs = out.parameter_equalities[action_idx].always_distinct;
            erase_if(pairs, [&](const auto &pair) {
                return atom.args[pair.first] == atom.args[pair.second];
            });
            auto inst = instantiate_action(
                action, atom.args, init_assignments, fluent_facts,
                objects_by_type, guard_domains, task.use_min_cost_metric);
            if (inst)
                out.instantiated_actions.push_back(move(*inst));
            break;
        }
        case grounding::PredicateRole::AXIOM: {
            int axiom_idx = roles.index_of(atom.predicate);
            const Axiom &axiom = task.axioms[axiom_idx];
            if (atom.args.size() < axiom.parameters.size())
                break;
            vector<string> args;
            args.reserve(axiom.parameters.size());
            for (size_t i = 0; i < axiom.parameters.size(); ++i)
                args.push_back(grounding::arg_to_string(atom.args[i]));
            auto inst =
                instantiate_axiom(axiom, args, fluent_facts, fact_by_id);
            if (inst)
                out.instantiated_axioms.push_back(move(inst));
            break;
        }
        case grounding::PredicateRole::OTHER:
            break;
        }
    }
    out.instantiated_goal =
        instantiate_goal(task.goal, fluent_facts, fact_by_id);
    return out;
}

void print_atom(ostream &out, const string &atom_str) {
    static const string prefix = "Atom ";
    if (atom_str.rfind(prefix, 0) == 0)
        out << atom_str.substr(prefix.size()) << "\n";
    else
        out << atom_str << "\n";
}

namespace {
/*
  Object names of each type, including the objects of all its subtypes. Types
  and objects appear in the order in which they are first inserted, matching
  the iteration order of Python's defaultdict in get_objects_by_type().
*/
vector<pair<string, vector<string>>> get_object_names_by_type(
    const Task &task) {
    unordered_map<string, vector<string>> supertypes;
    for (const auto &type : task.types)
        supertypes[type.name] = type.supertype_names;
    vector<pair<string, vector<string>>> result;
    unordered_map<string, size_t> position;
    auto add = [&](const string &type_name, const string &object) {
        auto [it, inserted] = position.emplace(type_name, result.size());
        if (inserted)
            result.emplace_back(type_name, vector<string>{});
        result[it->second].second.push_back(object);
    };
    for (const auto &obj : task.objects) {
        add(obj.type_name, obj.name);
        auto it = supertypes.find(obj.type_name);
        if (it != supertypes.end())
            for (const auto &supertype : it->second)
                add(supertype, obj.name);
    }
    return result;
}

void collect_predicate(const ConditionPtr &condition, set<string> &result) {
    if (!condition)
        return;
    if (condition->kind() == Condition::Kind::ATOM ||
        condition->kind() == Condition::Kind::NEGATED_ATOM) {
        result.insert(static_cast<const Literal &>(*condition).predicate);
    } else {
        for (const auto &part : condition->parts())
            collect_predicate(part, result);
    }
}
}

void dump_static_atoms(const Task &task, const vector<grounding::Atom> &model) {
    set<string> all_predicates;
    set<string> fluent_predicates;
    for (const auto &action : task.actions) {
        for (const auto &effect : action.effects) {
            if (!effect.literal)
                continue;
            const auto &literal = static_cast<const Literal &>(*effect.literal);
            fluent_predicates.insert(literal.predicate);
            all_predicates.insert(literal.predicate);
        }
        collect_predicate(action.precondition, all_predicates);
    }
    for (const auto &axiom : task.axioms)
        fluent_predicates.insert(axiom.name);

    set<string> static_predicates;
    ranges::set_difference(
        all_predicates, fluent_predicates,
        inserter(static_predicates, static_predicates.end()));

    ofstream out(STATIC_ATOMS_FILE);
    if (!out)
        utils::exit_with(
            utils::ExitCode::TRANSLATE_CRITICAL_ERROR,
            "Could not open output file: " + string(STATIC_ATOMS_FILE));
    for (const auto &atom : model) {
        if (!static_predicates.contains(atom.predicate_name()))
            continue;
        vector<string> args;
        args.reserve(atom.args.size());
        for (const auto &arg : atom.args)
            args.push_back(grounding::arg_to_string(arg));
        print_atom(out, Atom(atom.predicate_name(), move(args)).str());
    }
    for (const auto &[type_name, objects] : get_object_names_by_type(task))
        for (const auto &object : objects)
            print_atom(out, "Atom " + type_name + "(" + object + ")");
}
}
