#include "build.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/f_expression.h"
#include "../pddl/task.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace translate::grounding {
using namespace pddl;

namespace {
/*
  Build the Datalog rule body for a condition and a set of parameters.
  Mirrors normalize.condition_to_rule_body.

  - For each `parameter`, requires `type@T(par.name)`.
  - For an ExistentialCondition wrapper: requires `type@T(var)` for each
    bound var and then descends.
  - For a Conjunction, recurses over parts. For literals, only positive
    ones become part of the body.
  - For Falsity, returns a body of [@always-false] to make the rule
    unsatisfiable.
  - If a PNE is provided (only for action costs), require its definition
    predicate.
*/
std::vector<Atom> condition_to_rule_body(
    const std::vector<TypedObject> &parameters, const ConditionPtr &condition,
    const PrimitiveNumericExpression *pne) {
    std::vector<Atom> result;
    for (const auto &par : parameters) {
        result.emplace_back(
            type_predicate_name(par.type_name),
            std::vector<Arg>{Arg(par.name)});
    }
    if (condition && condition->kind() != Condition::Kind::TRUTH) {
        ConditionPtr cur = condition;
        if (cur->kind() == Condition::Kind::EXISTENTIAL) {
            const auto &q =
                static_cast<const ExistentialCondition &>(*cur);
            for (const auto &par : q.parameters) {
                result.emplace_back(
                    type_predicate_name(par.type_name),
                    std::vector<Arg>{Arg(par.name)});
            }
            cur = q.body[0];
        }
        std::vector<ConditionPtr> parts;
        if (cur->kind() == Condition::Kind::CONJUNCTION)
            parts = cur->parts();
        else
            parts = {cur};
        for (const auto &part : parts) {
            if (!part) continue;
            if (part->kind() == Condition::Kind::FALSITY) {
                return {Atom("@always-false", {})};
            }
            if (part->kind() != Condition::Kind::ATOM &&
                part->kind() != Condition::Kind::NEGATED_ATOM) {
                throw std::runtime_error(
                    "Condition not normalized: cannot build rule body");
            }
            const auto &lit = static_cast<const Literal &>(*part);
            if (!lit.negated()) {
                std::vector<Arg> args;
                args.reserve(lit.args.size());
                for (const auto &a : lit.args) args.emplace_back(a);
                result.emplace_back(lit.predicate, std::move(args));
            }
        }
    }
    if (pne) {
        // @def-<symbol>(pne.args...)
        std::vector<Arg> args;
        args.reserve(pne->args.size());
        for (const auto &a : pne->args) args.emplace_back(a);
        result.emplace_back("@def-" + pne->symbol, std::move(args));
    }
    return result;
}

Atom action_head(const Action &action) {
    std::vector<Arg> variables;
    variables.reserve(action.parameters.size());
    for (const auto &p : action.parameters) variables.emplace_back(p.name);
    if (action.precondition &&
        action.precondition->kind() == Condition::Kind::EXISTENTIAL) {
        const auto &q =
            static_cast<const ExistentialCondition &>(*action.precondition);
        for (const auto &p : q.parameters) variables.emplace_back(p.name);
    }
    return Atom(action.name, std::move(variables));
}

Atom axiom_head(const Axiom &axiom) {
    std::vector<Arg> variables;
    variables.reserve(axiom.parameters.size());
    for (const auto &p : axiom.parameters) variables.emplace_back(p.name);
    if (axiom.condition &&
        axiom.condition->kind() == Condition::Kind::EXISTENTIAL) {
        const auto &q =
            static_cast<const ExistentialCondition &>(*axiom.condition);
        for (const auto &p : q.parameters) variables.emplace_back(p.name);
    }
    return Atom(axiom.name, std::move(variables));
}

void add_typed_object(Program &prog, const TypedObject &obj,
                      const std::unordered_map<std::string,
                                               const Type *> &type_dict) {
    auto it = type_dict.find(obj.type_name);
    std::vector<std::string> chain;
    chain.push_back(obj.type_name);
    if (it != type_dict.end())
        for (const auto &sup : it->second->supertype_names)
            chain.push_back(sup);
    for (const auto &t : chain) {
        prog.add_fact(Atom(type_predicate_name(t),
                           std::vector<Arg>{Arg(obj.name)}));
    }
}

void translate_facts(Program &prog, const Task &task) {
    std::unordered_map<std::string, const Type *> type_dict;
    for (const auto &t : task.types) type_dict[t.name] = &t;
    for (const auto &obj : task.objects)
        add_typed_object(prog, obj, type_dict);
    for (const auto &elem : task.init) {
        if (auto *atom =
                std::get_if<std::shared_ptr<const pddl::Atom>>(&elem)) {
            if (*atom) {
                std::vector<Arg> args;
                args.reserve((*atom)->args.size());
                for (const auto &a : (*atom)->args) args.emplace_back(a);
                prog.add_fact(Atom((*atom)->predicate, std::move(args)));
            }
        } else if (auto *as = std::get_if<std::shared_ptr<Assign>>(&elem)) {
            if (*as && (*as)->fluent) {
                std::vector<Arg> args;
                args.reserve((*as)->fluent->args.size());
                for (const auto &a : (*as)->fluent->args)
                    args.emplace_back(a);
                prog.add_fact(
                    Atom("@def-" + (*as)->fluent->symbol, std::move(args)));
            }
        }
    }
}

void build_exploration_rules(Program &prog, const Task &task) {
    for (const auto &action : task.actions) {
        Atom head = action_head(action);
        const PrimitiveNumericExpression *pne = nullptr;
        if (action.cost && action.cost->expression &&
            action.cost->expression->kind() ==
                FunctionalExpression::Kind::PNE) {
            pne = static_cast<const PrimitiveNumericExpression *>(
                action.cost->expression.get());
        }
        auto body = condition_to_rule_body(action.parameters,
                                           action.precondition, pne);
        prog.add_rule(Rule{body, head});

        for (const auto &eff : action.effects) {
            if (!eff.literal) continue;
            const auto &lit = static_cast<const Literal &>(*eff.literal);
            if (lit.negated()) continue;
            std::vector<Atom> rule_body = {head};
            auto sub = condition_to_rule_body({}, eff.condition, nullptr);
            for (auto &c : sub) rule_body.push_back(std::move(c));
            std::vector<Arg> eff_args;
            eff_args.reserve(lit.args.size());
            for (const auto &a : lit.args) eff_args.emplace_back(a);
            prog.add_rule(Rule{rule_body, Atom(lit.predicate,
                                               std::move(eff_args))});
        }
    }
    for (const auto &axiom : task.axioms) {
        Atom app_head = axiom_head(axiom);
        auto app_body = condition_to_rule_body(axiom.parameters,
                                               axiom.condition, nullptr);
        prog.add_rule(Rule{app_body, app_head});
        // External params head.
        std::vector<Arg> eff_args;
        for (int i = 0; i < axiom.num_external_parameters; ++i)
            eff_args.emplace_back(axiom.parameters[i].name);
        Atom eff_head(axiom.name, std::move(eff_args));
        prog.add_rule(Rule{{app_head}, eff_head});
    }
    // Goal rule.
    if (task.goal) {
        Atom head("@goal-reachable", {});
        auto body = condition_to_rule_body({}, task.goal, nullptr);
        prog.add_rule(Rule{body, head});
    }
}
}

Program build_program(const Task &task) {
    Program prog;
    std::cout << "Generating Datalog program..." << std::endl;
    translate_facts(prog, task);
    build_exploration_rules(prog, task);
    std::cout << "Normalizing Datalog program..." << std::endl;
    prog.normalize();
    return prog;
}
}
