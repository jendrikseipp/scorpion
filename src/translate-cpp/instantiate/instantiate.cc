#include "instantiate.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/f_expression.h"
#include "../pddl/task.h"
#include "../translate_options.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace translate::instantiate {
using namespace pddl;

namespace {
constexpr const char *ACTION_PREFIX = "@a$";
constexpr const char *AXIOM_PREFIX = "@x$";
constexpr const char *GOAL_REACHABLE = "@goal-reachable";

int try_extract_index(const std::string &predicate, const char *prefix) {
    std::size_t pref_len = std::char_traits<char>::length(prefix);
    if (predicate.size() <= pref_len) return -1;
    if (predicate.compare(0, pref_len, prefix) != 0) return -1;
    try {
        return std::stoi(predicate.substr(pref_len));
    } catch (...) {
        return -1;
    }
}

std::unordered_set<std::string> get_fluent_predicates(const Task &task) {
    std::unordered_set<std::string> out;
    for (const auto &a : task.actions) {
        for (const auto &eff : a.effects) {
            if (eff.literal) {
                const auto &lit = static_cast<const Literal &>(*eff.literal);
                out.insert(lit.predicate);
            }
        }
    }
    for (const auto &x : task.axioms) out.insert(x.name);
    return out;
}

AtomSet build_atom_set(const std::vector<grounding::Atom> &model,
                       const std::unordered_set<std::string> &fluent_preds) {
    AtomSet out;
    for (const auto &a : model) {
        if (!fluent_preds.count(a.predicate)) continue;
        std::vector<std::string> args;
        args.reserve(a.args.size());
        for (const auto &x : a.args) {
            if (auto *s = std::get_if<std::string>(&x)) args.push_back(*s);
            else args.push_back(std::to_string(std::get<int>(x)));
        }
        out.insert(std::make_shared<const Atom>(a.predicate, std::move(args)));
    }
    return out;
}

AtomSet build_init_facts(const Task &task) {
    AtomSet out;
    for (const auto &elem : task.init) {
        if (auto *ap = std::get_if<std::shared_ptr<const Atom>>(&elem))
            if (*ap) out.insert(*ap);
    }
    return out;
}

// PNE-to-expression map for init assignments.
std::unordered_map<std::string,
                   std::shared_ptr<const FunctionalExpression>>
build_init_assignments(const Task &task) {
    std::unordered_map<std::string,
                       std::shared_ptr<const FunctionalExpression>> out;
    for (const auto &elem : task.init) {
        if (auto *as = std::get_if<std::shared_ptr<Assign>>(&elem)) {
            if (*as && (*as)->fluent) {
                std::string key = (*as)->fluent->symbol;
                for (const auto &a : (*as)->fluent->args) key += "\x1f" + a;
                out[key] = (*as)->expression;
            }
        }
    }
    return out;
}

std::unordered_map<std::string, std::vector<std::string>>
get_objects_by_type(const Task &task) {
    std::unordered_map<std::string, std::vector<std::string>> result;
    std::unordered_map<std::string, std::vector<std::string>> supertypes;
    for (const auto &t : task.types) supertypes[t.name] = t.supertype_names;
    for (const auto &obj : task.objects) {
        result[obj.type_name].push_back(obj.name);
        for (const auto &sup : supertypes[obj.type_name])
            result[sup].push_back(obj.name);
    }
    return result;
}

// Recursively iterate over the cartesian product of objects-by-type for
// each parameter, calling `fn(var_mapping)` for each assignment.
void for_each_assignment(
    const std::vector<TypedObject> &parameters,
    std::unordered_map<std::string, std::string> &var_mapping,
    const std::unordered_map<std::string,
                             std::vector<std::string>> &objects_by_type,
    const std::function<void()> &fn,
    std::size_t depth = 0) {
    if (depth == parameters.size()) {
        fn();
        return;
    }
    const auto &par = parameters[depth];
    auto it = objects_by_type.find(par.type_name);
    if (it == objects_by_type.end()) return;
    for (const auto &obj : it->second) {
        var_mapping[par.name] = obj;
        for_each_assignment(parameters, var_mapping, objects_by_type, fn,
                            depth + 1);
    }
}

void instantiate_effect(
    const Effect &eff,
    std::unordered_map<std::string, std::string> &var_mapping,
    const AtomSet &init_facts, const AtomSet &fluent_facts,
    const std::unordered_map<std::string,
                             std::vector<std::string>> &objects_by_type,
    std::vector<std::pair<std::vector<ConditionPtr>, ConditionPtr>> &result) {
    auto inst_once = [&]() {
        std::vector<ConditionPtr> condition;
        try {
            if (eff.condition)
                eff.condition->instantiate(var_mapping, init_facts,
                                           fluent_facts, condition);
        } catch (const Impossible &) {
            return;
        }
        std::vector<ConditionPtr> lit_out;
        try {
            if (eff.literal)
                eff.literal->instantiate(var_mapping, init_facts, fluent_facts,
                                         lit_out);
        } catch (const Impossible &) {
            return;
        }
        if (!lit_out.empty()) {
            result.emplace_back(std::move(condition), std::move(lit_out[0]));
        }
    };
    if (eff.parameters.empty()) {
        inst_once();
    } else {
        for_each_assignment(eff.parameters, var_mapping, objects_by_type,
                            inst_once);
    }
}

long long evaluate_constant(const FunctionalExpression &expr) {
    if (expr.kind() == FunctionalExpression::Kind::CONSTANT) {
        return static_cast<const NumericConstant &>(expr).value;
    }
    throw std::runtime_error("cost expression is not a numeric constant");
}

std::shared_ptr<PropositionalAction> instantiate_action(
    const Action &action, const std::vector<std::string> &args,
    const AtomSet &init_facts,
    const std::unordered_map<std::string,
                             std::shared_ptr<const FunctionalExpression>>
        &init_assignments,
    const AtomSet &fluent_facts,
    const std::unordered_map<std::string,
                             std::vector<std::string>> &objects_by_type,
    bool use_metric) {
    if (args.size() != action.parameters.size())
        return nullptr;
    std::unordered_map<std::string, std::string> var_mapping;
    for (std::size_t i = 0; i < action.parameters.size(); ++i)
        var_mapping[action.parameters[i].name] = args[i];

    // Build the grounded name using only external parameters.
    //
    // We mirror Python's `"(%s %s)" % (action.name, " ".join(args))`
    // exactly -- that format always emits a space after action.name,
    // even when the args list is empty, producing "(name )" (with a
    // space before the close paren). After SAS-output paren-stripping
    // this becomes a trailing-space in the operator name, which is
    // load-bearing for byte-identical output and stable sort key.
    std::string name = "(" + action.name + " ";
    for (int i = 0; i < action.num_external_parameters; ++i) {
        if (i > 0) name.push_back(' ');
        name += args[i];
    }
    name.push_back(')');

    std::vector<ConditionPtr> precondition;
    try {
        if (action.precondition)
            action.precondition->instantiate(var_mapping, init_facts,
                                             fluent_facts, precondition);
    } catch (const Impossible &) {
        return nullptr;
    }

    std::vector<std::pair<std::vector<ConditionPtr>, ConditionPtr>> effects;
    for (const auto &eff : action.effects) {
        std::unordered_map<std::string, std::string> local_mapping =
            var_mapping;
        instantiate_effect(eff, local_mapping, init_facts, fluent_facts,
                           objects_by_type, effects);
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
                    std::vector<std::string> resolved_args;
                    resolved_args.reserve(pne.args.size());
                    for (const auto &a : pne.args) {
                        auto it = var_mapping.find(a);
                        resolved_args.push_back(
                            it == var_mapping.end() ? a : it->second);
                    }
                    std::string key = pne.symbol;
                    for (const auto &a : resolved_args)
                        key += "\x1f" + a;
                    auto it = init_assignments.find(key);
                    if (it == init_assignments.end())
                        throw std::runtime_error(
                            "Could not find PNE initialization for cost");
                    cost = evaluate_constant(*it->second);
                } else {
                    cost = evaluate_constant(*action.cost->expression);
                }
            } else {
                cost = 0;
            }
        }
        return std::make_shared<PropositionalAction>(
            name, std::move(precondition), std::move(effects),
            static_cast<int>(cost));
    }
    return nullptr;
}

std::shared_ptr<PropositionalAxiom> instantiate_axiom(
    const Axiom &axiom, const std::vector<std::string> &args,
    const AtomSet &init_facts, const AtomSet &fluent_facts) {
    if (args.size() != axiom.parameters.size())
        return nullptr;
    std::unordered_map<std::string, std::string> var_mapping;
    for (std::size_t i = 0; i < axiom.parameters.size(); ++i)
        var_mapping[axiom.parameters[i].name] = args[i];

    std::vector<std::string> name_args;
    name_args.push_back(axiom.name);
    for (int i = 0; i < axiom.num_external_parameters; ++i)
        name_args.push_back(args[i]);
    std::string name = "(";
    for (std::size_t i = 0; i < name_args.size(); ++i) {
        if (i) name.push_back(' ');
        name += name_args[i];
    }
    name.push_back(')');

    std::vector<ConditionPtr> condition;
    try {
        if (axiom.condition)
            axiom.condition->instantiate(var_mapping, init_facts,
                                         fluent_facts, condition);
    } catch (const Impossible &) {
        return nullptr;
    }

    std::vector<std::string> eff_args;
    eff_args.reserve(axiom.num_external_parameters);
    for (int i = 0; i < axiom.num_external_parameters; ++i) {
        const auto &n = axiom.parameters[i].name;
        auto it = var_mapping.find(n);
        eff_args.push_back(it == var_mapping.end() ? n : it->second);
    }
    auto effect = std::make_shared<const Atom>(axiom.name,
                                                std::move(eff_args));
    return std::make_shared<PropositionalAxiom>(
        std::move(name), std::move(condition), std::move(effect));
}

std::optional<std::vector<ConditionPtr>> instantiate_goal(
    const ConditionPtr &goal, const AtomSet &init_facts,
    const AtomSet &fluent_facts) {
    std::vector<ConditionPtr> result;
    std::unordered_map<std::string, std::string> empty;
    try {
        if (goal) goal->instantiate(empty, init_facts, fluent_facts, result);
    } catch (const Impossible &) {
        return std::nullopt;
    }
    return result;
}
}

Result instantiate(const Task &task,
                   const std::vector<grounding::Atom> &model) {
    Result out;
    out.reachable_action_parameters.resize(task.actions.size());
    auto fluent_preds = get_fluent_predicates(task);
    out.fluent_facts = build_atom_set(model, fluent_preds);
    auto init_facts = build_init_facts(task);
    auto init_assignments = build_init_assignments(task);
    auto objects_by_type = get_objects_by_type(task);

    for (const auto &atom : model) {
        if (atom.predicate == GOAL_REACHABLE) {
            out.relaxed_reachable = true;
            continue;
        }
        int action_idx = try_extract_index(atom.predicate, ACTION_PREFIX);
        if (action_idx >= 0 &&
            action_idx < static_cast<int>(task.actions.size())) {
            const Action &action = task.actions[action_idx];
            if (atom.args.size() < action.parameters.size()) continue;
            std::vector<std::string> args;
            args.reserve(action.parameters.size());
            for (std::size_t i = 0; i < action.parameters.size(); ++i) {
                if (auto *s = std::get_if<std::string>(&atom.args[i]))
                    args.push_back(*s);
                else
                    args.push_back(std::to_string(std::get<int>(atom.args[i])));
            }
            auto inst = instantiate_action(action, args, init_facts,
                                           init_assignments, out.fluent_facts,
                                           objects_by_type,
                                           task.use_min_cost_metric);
            // Move args into reachable_action_parameters after the
            // instantiate_action call, saving one vector<string> copy
            // per processed model atom.
            out.reachable_action_parameters[action_idx].push_back(
                std::move(args));
            if (inst) out.instantiated_actions.push_back(std::move(inst));
            continue;
        }
        int axiom_idx = try_extract_index(atom.predicate, AXIOM_PREFIX);
        if (axiom_idx >= 0 &&
            axiom_idx < static_cast<int>(task.axioms.size())) {
            const Axiom &axiom = task.axioms[axiom_idx];
            if (atom.args.size() < axiom.parameters.size()) continue;
            std::vector<std::string> args;
            args.reserve(axiom.parameters.size());
            for (std::size_t i = 0; i < axiom.parameters.size(); ++i) {
                if (auto *s = std::get_if<std::string>(&atom.args[i]))
                    args.push_back(*s);
                else
                    args.push_back(std::to_string(std::get<int>(atom.args[i])));
            }
            auto inst = instantiate_axiom(axiom, args, init_facts,
                                          out.fluent_facts);
            if (inst) out.instantiated_axioms.push_back(std::move(inst));
            continue;
        }
    }
    out.instantiated_goal = instantiate_goal(task.goal, init_facts,
                                             out.fluent_facts);
    return out;
}
}
