#include "instantiate.h"

#include "../translate_options.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/f_expression.h"
#include "../pddl/task.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using namespace std;
namespace translate::instantiate {
using namespace pddl;

namespace {
// Interned ids of the predicates that can appear as (action/axiom) effects.
// Comparing interned ids lets build_atom_set test each model atom with an int
// lookup instead of hashing its predicate name; equal names always intern to
// the same id, so the membership test is equivalent.
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

// Build the reachable fluent facts in two shapes that share one Atom object
// each: the string AtomSet returned in the Result (consumed by fact_groups)
// and the integer-keyed FluentFactMap used for the hot instantiation probe.
// The model atoms already carry interned predicate/object ids, so the integer
// key is read straight off them.
struct FluentFacts {
    AtomSet set;         // for the Result / fact_groups
    FluentFactMap by_id; // for probing (GroundKey -> canonical Atom)
};

FluentFacts build_fluent_facts(
    const vector<grounding::Atom> &model,
    const unordered_set<int> &fluent_preds) {
    FluentFacts out;
    for (const auto &a : model) {
        if (!fluent_preds.contains(a.predicate))
            continue;
        vector<string> args;
        args.reserve(a.args.size());
        for (const auto &x : a.args)
            args.push_back(grounding::arg_to_string(x));
        auto atom = make_shared<const Atom>(a.predicate_name(), move(args));
        GroundKey key;
        key.predicate = a.predicate;
        for (const auto &x : a.args)
            key.args.push_back(x.v);
        out.set.insert(atom);
        out.by_id.emplace(move(key), move(atom));
    }
    return out;
}

// Static init facts, keyed by integer ground key for the instantiation probe.
InitFactSet build_init_facts(const Task &task) {
    InitFactSet out;
    for (const auto &elem : task.init) {
        auto *ap = get_if<shared_ptr<const Atom>>(&elem);
        if (!ap || !*ap)
            continue;
        GroundKey key;
        key.predicate = grounding::symbols().intern((*ap)->predicate);
        for (const auto &arg : (*ap)->args)
            key.args.push_back(grounding::symbols().intern(arg));
        out.insert(move(key));
    }
    return out;
}

// PNE-to-expression map for init assignments.
unordered_map<string, shared_ptr<const FunctionalExpression>>
build_init_assignments(const Task &task) {
    unordered_map<string, shared_ptr<const FunctionalExpression>> out;
    for (const auto &elem : task.init) {
        if (auto *as = get_if<shared_ptr<Assign>>(&elem)) {
            if (*as && (*as)->fluent) {
                string key = (*as)->fluent->symbol;
                for (const auto &a : (*as)->fluent->args)
                    key += "\x1f" + a;
                out[key] = (*as)->expression;
            }
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
    const vector<TypedObject> &parameters,
    VarMapping &var_mapping,
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
    const Effect &eff, VarMapping &var_mapping,
    const InitFactSet &init_facts, const FluentFactMap &fluent_facts,
    const unordered_map<string, vector<int>> &objects_by_type,
    vector<pair<vector<ConditionPtr>, ConditionPtr>> &result) {
    auto inst_once = [&]() {
        vector<ConditionPtr> condition;
        if (eff.condition &&
            !eff.condition->instantiate(
                var_mapping, init_facts, fluent_facts, condition))
            return;
        vector<ConditionPtr> lit_out;
        if (eff.literal && !eff.literal->instantiate(
                               var_mapping, init_facts, fluent_facts, lit_out))
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

shared_ptr<PropositionalAction> instantiate_action(
    const Action &action, const vector<string> &args,
    const InitFactSet &init_facts,
    const unordered_map<string, shared_ptr<const FunctionalExpression>>
        &init_assignments,
    const FluentFactMap &fluent_facts,
    const unordered_map<string, vector<int>> &objects_by_type,
    bool use_metric) {
    if (args.size() != action.parameters.size())
        return nullptr;
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

    vector<ConditionPtr> precondition;
    if (action.precondition &&
        !action.precondition->instantiate(
            var_mapping, init_facts, fluent_facts, precondition))
        return nullptr;

    vector<pair<vector<ConditionPtr>, ConditionPtr>> effects;
    for (const auto &eff : action.effects) {
        if (eff.parameters.empty()) {
            // A parameterless effect adds no bindings, and instantiate()
            // only reads var_mapping, so share the action's mapping
            // directly instead of copying the whole map per effect.
            instantiate_effect(
                eff, var_mapping, init_facts, fluent_facts, objects_by_type,
                effects);
        } else {
            VarMapping local_mapping = var_mapping;
            instantiate_effect(
                eff, local_mapping, init_facts, fluent_facts, objects_by_type,
                effects);
        }
    }
    if (!effects.empty() || get_options().keep_no_ops) {
        long long cost = 1;
        if (use_metric) {
            if (action.cost && action.cost->expression) {
                // Instantiate the cost expression: if it's a PNE, look up
                // in init_assignments; else if it's a constant, use it.
                if (action.cost->expression->kind() ==
                    FunctionalExpression::Kind::PNE) {
                    const auto &pne =
                        static_cast<const PrimitiveNumericExpression &>(
                            *action.cost->expression);
                    vector<string> resolved_args;
                    resolved_args.reserve(pne.args.size());
                    for (const auto &a : pne.args) {
                        auto it = var_mapping.find(a);
                        resolved_args.push_back(
                            it == var_mapping.end()
                                ? a
                                : grounding::symbols().name(it->second));
                    }
                    string key = pne.symbol;
                    for (const auto &a : resolved_args)
                        key += "\x1f" + a;
                    auto it = init_assignments.find(key);
                    if (it == init_assignments.end())
                        throw runtime_error(
                            "Could not find PNE initialization for cost");
                    cost = evaluate_constant(*it->second);
                } else {
                    cost = evaluate_constant(*action.cost->expression);
                }
            } else {
                cost = 0;
            }
        }
        return make_shared<PropositionalAction>(
            name, move(precondition), move(effects), static_cast<int>(cost));
    }
    return nullptr;
}

shared_ptr<PropositionalAxiom> instantiate_axiom(
    const Axiom &axiom, const vector<string> &args,
    const InitFactSet &init_facts, const FluentFactMap &fluent_facts) {
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

    vector<ConditionPtr> condition;
    if (axiom.condition &&
        !axiom.condition->instantiate(
            var_mapping, init_facts, fluent_facts, condition))
        return nullptr;

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
    const ConditionPtr &goal, const InitFactSet &init_facts,
    const FluentFactMap &fluent_facts) {
    vector<ConditionPtr> result;
    VarMapping empty;
    if (goal && !goal->instantiate(empty, init_facts, fluent_facts, result))
        return nullopt;
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
    const FluentFactMap &fluent_facts = fluent.by_id;
    auto init_facts = build_init_facts(task);
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
            for (size_t i = 0; i < action.parameters.size(); ++i)
                args.push_back(grounding::arg_to_string(atom.args[i]));
            auto inst = instantiate_action(
                action, args, init_facts, init_assignments, fluent_facts,
                objects_by_type, task.use_min_cost_metric);
            // Move args into reachable_action_parameters after the
            // instantiate_action call, saving one vector<string> copy
            // per processed model atom.
            out.reachable_action_parameters[action_idx].push_back(move(args));
            if (inst)
                out.instantiated_actions.push_back(move(inst));
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
                instantiate_axiom(axiom, args, init_facts, fluent_facts);
            if (inst)
                out.instantiated_axioms.push_back(move(inst));
            break;
        }
        case grounding::PredicateRole::OTHER:
            break;
        }
    }
    out.instantiated_goal =
        instantiate_goal(task.goal, init_facts, fluent_facts);
    return out;
}
}
