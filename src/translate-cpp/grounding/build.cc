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

using namespace std;
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
vector<Atom> condition_to_rule_body(
    const vector<TypedObject> &parameters, const ConditionPtr &condition,
    const PrimitiveNumericExpression *pne) {
    vector<Atom> result;
    for (const auto &par : parameters) {
        result.emplace_back(
            type_predicate_name(par.type_name),
            ArgList{Arg(par.name)});
    }
    if (condition && condition->kind() != Condition::Kind::TRUTH) {
        ConditionPtr cur = condition;
        if (cur->kind() == Condition::Kind::EXISTENTIAL) {
            const auto &q =
                static_cast<const ExistentialCondition &>(*cur);
            for (const auto &par : q.parameters) {
                result.emplace_back(
                    type_predicate_name(par.type_name),
                    ArgList{Arg(par.name)});
            }
            cur = q.body[0];
        }
        vector<ConditionPtr> parts;
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
                throw runtime_error(
                    "Condition not normalized: cannot build rule body");
            }
            const auto &lit = static_cast<const Literal &>(*part);
            if (!lit.negated()) {
                ArgList args;
                args.reserve(lit.args.size());
                for (const auto &a : lit.args) args.emplace_back(a);
                result.emplace_back(lit.predicate, move(args));
            }
        }
    }
    if (pne) {
        // @def-<symbol>(pne.args...)
        ArgList args;
        args.reserve(pne->args.size());
        for (const auto &a : pne->args) args.emplace_back(a);
        result.emplace_back("@def-" + pne->symbol, move(args));
    }
    return result;
}

/*
  Datalog head predicate names. Python uses the Action/Axiom object as
  the predicate; in C++ we encode the (action/axiom) index so the
  instantiate pass can map back to the source action/axiom even when
  multiple actions share a name (e.g., after split_disjunctions).
*/
string action_head_predicate(int action_index) {
    return "@a$" + to_string(action_index);
}
string axiom_head_predicate(int axiom_index) {
    return "@x$" + to_string(axiom_index);
}

Atom action_head(const Action &action, int action_index) {
    ArgList variables;
    variables.reserve(action.parameters.size());
    for (const auto &p : action.parameters) variables.emplace_back(p.name);
    if (action.precondition &&
        action.precondition->kind() == Condition::Kind::EXISTENTIAL) {
        const auto &q =
            static_cast<const ExistentialCondition &>(*action.precondition);
        for (const auto &p : q.parameters) variables.emplace_back(p.name);
    }
    return Atom(action_head_predicate(action_index), move(variables));
}

Atom axiom_head(const Axiom &axiom, int axiom_index) {
    ArgList variables;
    variables.reserve(axiom.parameters.size());
    for (const auto &p : axiom.parameters) variables.emplace_back(p.name);
    if (axiom.condition &&
        axiom.condition->kind() == Condition::Kind::EXISTENTIAL) {
        const auto &q =
            static_cast<const ExistentialCondition &>(*axiom.condition);
        for (const auto &p : q.parameters) variables.emplace_back(p.name);
    }
    return Atom(axiom_head_predicate(axiom_index), move(variables));
}

void add_typed_object(Program &prog, const TypedObject &obj,
                      const unordered_map<string,
                                               const Type *> &type_dict) {
    auto it = type_dict.find(obj.type_name);
    vector<string> chain;
    chain.push_back(obj.type_name);
    if (it != type_dict.end())
        for (const auto &sup : it->second->supertype_names)
            chain.push_back(sup);
    for (const auto &t : chain) {
        prog.add_fact(Atom(type_predicate_name(t),
                           ArgList{Arg(obj.name)}));
    }
}

void translate_facts(Program &prog, const Task &task) {
    unordered_map<string, const Type *> type_dict;
    for (const auto &t : task.types) type_dict[t.name] = &t;
    for (const auto &obj : task.objects)
        add_typed_object(prog, obj, type_dict);
    for (const auto &elem : task.init) {
        if (auto *atom =
                get_if<shared_ptr<const pddl::Atom>>(&elem)) {
            if (*atom) {
                ArgList args;
                args.reserve((*atom)->args.size());
                for (const auto &a : (*atom)->args) args.emplace_back(a);
                prog.add_fact(Atom((*atom)->predicate, move(args)));
            }
        } else if (auto *as = get_if<shared_ptr<Assign>>(&elem)) {
            if (*as && (*as)->fluent) {
                ArgList args;
                args.reserve((*as)->fluent->args.size());
                for (const auto &a : (*as)->fluent->args)
                    args.emplace_back(a);
                prog.add_fact(
                    Atom("@def-" + (*as)->fluent->symbol, move(args)));
            }
        }
    }
}

void build_exploration_rules(Program &prog, const Task &task) {
    for (size_t i = 0; i < task.actions.size(); ++i) {
        const Action &action = task.actions[i];
        Atom head = action_head(action, static_cast<int>(i));
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
            vector<Atom> rule_body = {head};
            auto sub = condition_to_rule_body({}, eff.condition, nullptr);
            for (auto &c : sub) rule_body.push_back(move(c));
            ArgList eff_args;
            eff_args.reserve(lit.args.size());
            for (const auto &a : lit.args) eff_args.emplace_back(a);
            prog.add_rule(Rule{rule_body, Atom(lit.predicate,
                                               move(eff_args))});
        }
    }
    for (size_t i = 0; i < task.axioms.size(); ++i) {
        const Axiom &axiom = task.axioms[i];
        Atom app_head = axiom_head(axiom, static_cast<int>(i));
        auto app_body = condition_to_rule_body(axiom.parameters,
                                               axiom.condition, nullptr);
        prog.add_rule(Rule{app_body, app_head});
        // External params head.
        ArgList eff_args;
        for (int j = 0; j < axiom.num_external_parameters; ++j)
            eff_args.emplace_back(axiom.parameters[j].name);
        Atom eff_head(axiom.name, move(eff_args));
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
    cout << "Generating Datalog program..." << endl;
    translate_facts(prog, task);
    build_exploration_rules(prog, task);
    cout << "Normalizing Datalog program..." << endl;
    prog.normalize();
    return prog;
}
}
