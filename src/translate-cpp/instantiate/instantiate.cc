#include "instantiate.h"

#include "../translate_options.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/f_expression.h"
#include "../pddl/task.h"
#include "../utils/hash.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
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

// Three shared shapes of the reachable fluent facts (one Atom per fact): the
// AtomSet in the Result (fact_groups), the FactMap GroundKey->FactId (the
// instantiation probe + building the FactId->(var,val) table), and fact_by_id
// FactId->Atom (rebuilding the few axiom/goal literals as atoms). Each distinct
// fluent fact gets a dense FactId in model order.
struct FluentFacts {
    AtomSet set; // for fact_groups
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
        out.set.insert(atom);
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
    vector<GroundEffect> &result) {
    auto inst_once = [&]() {
        vector<GroundLiteral> condition;
        if (eff.condition &&
            !eff.condition->instantiate(var_mapping, fluent_facts, condition))
            return;
        vector<GroundLiteral> lit_out;
        if (eff.literal &&
            !eff.literal->instantiate(var_mapping, fluent_facts, lit_out))
            return;
        if (!lit_out.empty()) {
            result.emplace_back(move(condition), move(lit_out[0]));
        }
    };
    if (eff.parameters.empty()) {
        inst_once();
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
    const Action &action, const vector<string> &args,
    const InitAssignments &init_assignments, const FactMap &fluent_facts,
    const unordered_map<string, vector<int>> &objects_by_type,
    bool use_metric) {
    if (args.size() != action.parameters.size())
        return nullopt;
    // Reused across ground actions (instantiate_action is never re-entrant):
    // clear() keeps the backing buffer, so rebinding per ground action neither
    // frees nor re-allocates in the dominant instantiation phase. Parameterised
    // effects still take their own copy before binding extra parameters.
    static thread_local VarMapping var_mapping;
    var_mapping.clear();
    for (size_t i = 0; i < action.parameters.size(); ++i)
        var_mapping[action.parameters[i].name] =
            grounding::symbols().intern(args[i]);

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
        name += args[i];
    }
    name.push_back(')');

    vector<GroundLiteral> precondition;
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
                eff, var_mapping, fluent_facts, objects_by_type, effects);
        } else {
            VarMapping local_mapping = var_mapping;
            instantiate_effect(
                eff, local_mapping, fluent_facts, objects_by_type, effects);
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

    vector<GroundLiteral> condition_lits;
    if (axiom.condition && !axiom.condition->instantiate(
                               var_mapping, fluent_facts, condition_lits))
        return nullptr;
    // Axioms are few: keep the downstream (axiom_rules) atom-based.
    vector<ConditionPtr> condition;
    condition.reserve(condition_lits.size());
    for (const auto &l : condition_lits)
        condition.push_back(to_condition(l, fact_by_id));

    vector<string> eff_args;
    eff_args.reserve(axiom.num_external_parameters);
    for (int i = 0; i < axiom.num_external_parameters; ++i) {
        const auto &n = axiom.parameters[i].name;
        auto it = var_mapping.find(n);
        eff_args.push_back(
            it == var_mapping.end() ? n
                                    : grounding::symbols().name(it->second));
    }
    auto effect = make_shared<const Atom>(axiom.name, move(eff_args));
    return make_shared<PropositionalAxiom>(
        move(name), move(condition), move(effect));
}

optional<vector<ConditionPtr>> instantiate_goal(
    const ConditionPtr &goal, const FactMap &fluent_facts,
    const vector<shared_ptr<const Atom>> &fact_by_id) {
    vector<GroundLiteral> lits;
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
    out.reachable_action_parameters.resize(task.actions.size());
    auto fluent_preds = get_fluent_predicates(task);
    auto fluent = build_fluent_facts(model, fluent_preds);
    out.fluent_facts = move(fluent.set);
    out.fluent_fact_ids = move(fluent.ids);
    out.fact_by_id = move(fluent.fact_by_id);
    const FactMap &fluent_facts = out.fluent_fact_ids;
    const auto &fact_by_id = out.fact_by_id;
    add_static_init_facts(task, out.fluent_fact_ids);
    auto init_assignments = build_init_assignments(task);
    auto objects_by_type = get_objects_by_type(task);

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
            vector<string> args;
            args.reserve(action.parameters.size());
            // Interned object ids for the reachable-parameters table (kept for
            // invariant finding). Storing ids instead of the argument strings
            // shrinks this long-lived table ~8x and lets its only consumer
            // (BalanceChecker's "ever equal?" test) compare ints; equal object
            // names always intern to the same id, so the test is unchanged.
            vector<int> arg_ids;
            arg_ids.reserve(action.parameters.size());
            for (size_t i = 0; i < action.parameters.size(); ++i) {
                args.push_back(grounding::arg_to_string(atom.args[i]));
                arg_ids.push_back(atom.args[i].v);
            }
            auto inst = instantiate_action(
                action, args, init_assignments, fluent_facts, objects_by_type,
                task.use_min_cost_metric);
            out.reachable_action_parameters[action_idx].push_back(
                move(arg_ids));
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
}
