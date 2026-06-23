#include "parser.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/f_expression.h"
#include "../pddl/predicate.h"
#include "../pddl/task.h"
#include "../pddl/types.h"
#include "../translate_options.h"
#include "../utils/graph.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using namespace std;
namespace translate::parser {
using pddl::Action;
using pddl::Atom;
using pddl::Axiom;
using pddl::Condition;
using pddl::ConditionPtr;
using pddl::Conjunction;
using pddl::Disjunction;
using pddl::Effect;
using pddl::ExistentialCondition;
using pddl::Function;
using pddl::Increase;
using pddl::NegatedAtom;
using pddl::Predicate;
using pddl::Requirements;
using pddl::Type;
using pddl::TypedObject;
using pddl::UniversalCondition;

namespace {
/* ----------------------------- Context ------------------------------ */

class Context {
public:
    vector<string> trace;

    [[noreturn]] void error(const string &message,
                            const Sexpr *item = nullptr,
                            const char *syntax = nullptr) const {
        ostringstream os;
        for (size_t i = 0; i < trace.size(); ++i)
            os << (i ? "\n\t->" : "") << trace[i];
        os << "\n" << message;
        if (syntax) os << "\nSyntax: " << syntax;
        if (item) {
            os << "\nGot: ";
            write_lispified(os, *item);
        }
        throw ParseError(os.str());
    }

    struct Layer {
        Context *ctx;
        explicit Layer(Context *c, string s) : ctx(c) {
            ctx->trace.push_back(move(s));
        }
        ~Layer() { ctx->trace.pop_back(); }
        Layer(const Layer &) = delete;
        Layer &operator=(const Layer &) = delete;
    };

    Layer layer(string description) {
        return Layer(this, move(description));
    }
};

/* ----------------------------- warnings ----------------------------- */

set<string> printed_warnings;

void print_warning(const string &msg) {
    if (printed_warnings.insert(msg).second)
        cerr << "Warning: " << msg << "\n";
}

/* ----------------------------- helpers ------------------------------ */

constexpr const char *SYNTAX_LITERAL = "(PREDICATE ARGUMENTS*)";
constexpr const char *SYNTAX_LITERAL_NEGATED = "(not (PREDICATE ARGUMENTS*))";
constexpr const char *SYNTAX_PREDICATE =
    "(PREDICATE_NAME [VARIABLE [- TYPE]?]*)";
constexpr const char *SYNTAX_FUNCTION =
    "(FUNCTION_NAME [VARIABLE [- TYPE]?]*)";
constexpr const char *SYNTAX_ACTION =
    "(:action NAME [:parameters PARAMETERS]? "
    "[:precondition PRECONDITION]? :effect EFFECT)";
constexpr const char *SYNTAX_AXIOM = "(:derived PREDICATE CONDITION)";
constexpr const char *SYNTAX_GOAL = "(:goal GOAL)";
constexpr const char *SYNTAX_CONDITION_AND = "(and CONDITION*)";
constexpr const char *SYNTAX_CONDITION_OR = "(or CONDITION*)";
constexpr const char *SYNTAX_CONDITION_IMPLY = "(imply CONDITION CONDITION)";
constexpr const char *SYNTAX_CONDITION_NOT = "(not CONDITION)";
constexpr const char *SYNTAX_CONDITION_FORALL_EXISTS =
    "({forall, exists} VARIABLES CONDITION)";
constexpr const char *SYNTAX_EFFECT_FORALL = "(forall VARIABLES EFFECT)";
constexpr const char *SYNTAX_EFFECT_WHEN = "(when CONDITION EFFECT)";
constexpr const char *SYNTAX_EFFECT_INCREASE =
    "(increase (total-cost) ASSIGNMENT)";
constexpr const char *SYNTAX_EXPRESSION =
    "POSITIVE_NUMBER or (FUNCTION VARIABLES*)";
constexpr const char *SYNTAX_ASSIGNMENT =
    "({=,increase} EXPRESSION EXPRESSION)";
constexpr const char *SYNTAX_DOMAIN_DOMAIN_NAME = "(domain NAME)";
constexpr const char *SYNTAX_TASK_PROBLEM_NAME = "(problem NAME)";
constexpr const char *SYNTAX_TASK_DOMAIN_NAME = "(:domain NAME)";
constexpr const char *SYNTAX_METRIC = "(:metric minimize (total-cost))";

const string TYPED_LIST_SEPARATOR = "-";

const char *condition_tag_to_syntax(const string &tag) {
    if (tag == "and") return SYNTAX_CONDITION_AND;
    if (tag == "or") return SYNTAX_CONDITION_OR;
    if (tag == "imply") return SYNTAX_CONDITION_IMPLY;
    if (tag == "not") return SYNTAX_CONDITION_NOT;
    if (tag == "forall" || tag == "exists")
        return SYNTAX_CONDITION_FORALL_EXISTS;
    return nullptr;
}

void check_word(Context &ctx, const Sexpr &word, const char *description,
                const char *syntax = nullptr) {
    if (!word.is_atom())
        ctx.error(string(description) + " is expected to be a word.",
                  &word, syntax);
}

void check_list(Context &ctx, const Sexpr &alist, const char *description,
                const char *syntax = nullptr) {
    if (!alist.is_list())
        ctx.error(string(description) + " is expected to be a block.",
                  &alist, syntax);
}

void check_named_block(Context &ctx, const Sexpr &alist,
                       const vector<string> &names,
                       const char *syntax = nullptr) {
    bool ok = alist.is_list() && !alist.list().empty() &&
              alist.list()[0].is_atom() &&
              ranges::find(names,
                        alist.list()[0].atom()) != names.end();
    if (!ok) {
        string msg = "Expected a non-empty block starting with any of "
                          "the following words: ";
        for (size_t i = 0; i < names.size(); ++i)
            msg += (i ? ", " : "") + names[i];
        ctx.error(msg, &alist, syntax);
    }
}

bool starts_with_qmark(const string &s) {
    return !s.empty() && s.front() == '?';
}

/* ---------------------------- typed lists --------------------------- */

TypedObject construct_typed_object(Context &ctx, const Sexpr &name,
                                   const Sexpr &type_name) {
    auto layer = ctx.layer("Parsing typed object");
    check_word(ctx, name, "Name of typed object");
    // The type may be a list (for `either` types) in predicate definitions,
    // so we don't validate here.
    if (type_name.is_atom())
        return TypedObject(name.atom(), type_name.atom());
    // Either-type: serialize as "(either x y ...)" to preserve information.
    return TypedObject(name.atom(), lispified(type_name));
}

Type construct_type(Context &ctx, const Sexpr &curr_type,
                    const Sexpr &base_type) {
    auto layer = ctx.layer("Parsing PDDL type");
    check_word(ctx, curr_type, "PDDL type");
    check_word(ctx, base_type, "Base type");
    return Type(curr_type.atom(), base_type.atom());
}

/*
  Parse a typed list of objects or types: `x1 x2 - T1 y1 y2 - T2 z`.
  - If `only_variables`, each item must start with `?`.
  - If `either_allowed`, the type may be a `(either ...)` block (used in
    predicate parameter lists).
  - `constructor` controls how items are constructed (typed object or type).
*/
template<class Item, class Construct>
vector<Item> parse_typed_list_typed(
    Context &ctx, const SexprList &alist, bool only_variables,
    bool either_allowed, const Construct &constructor,
    const string &default_type = "object") {
    auto layer = ctx.layer("Parsing typed list");
    vector<Item> result;
    size_t cursor = 0;
    int group_number = 1;
    while (cursor < alist.size()) {
        auto group_layer = ctx.layer(
            "Parsing " + to_string(group_number) + ". group of typed list");
        // Find separator '-' starting from cursor.
        size_t sep = cursor;
        for (; sep < alist.size(); ++sep) {
            if (alist[sep].is_atom() &&
                alist[sep].atom() == TYPED_LIST_SEPARATOR)
                break;
        }
        Sexpr type_sexpr;
        size_t items_end;
        size_t next_cursor;
        if (sep == alist.size()) {
            items_end = alist.size();
            type_sexpr = Sexpr(default_type);
            next_cursor = alist.size();
        } else {
            if (sep == alist.size() - 1) {
                ctx.error("Type missing after '" + TYPED_LIST_SEPARATOR + "'.",
                          &alist[sep]);
            }
            items_end = sep;
            if (items_end == cursor) {
                ostringstream os;
                write_lispified(os, Sexpr(alist));
                print_warning("Expected something before the separator '" +
                              TYPED_LIST_SEPARATOR + "'. Got: " + os.str());
            }
            type_sexpr = alist[sep + 1];
            next_cursor = sep + 2;
            if (!type_sexpr.is_atom()) {
                if (!either_allowed) {
                    ctx.error("Type value is expected to be a single word.",
                              &type_sexpr);
                } else if (type_sexpr.list().empty() ||
                           !type_sexpr.list()[0].is_atom() ||
                           type_sexpr.list()[0].atom() != "either") {
                    ctx.error("Type value is expected to be a single word "
                              "or '(either WORD*)'", &type_sexpr);
                }
            }
        }
        for (size_t i = cursor; i < items_end; ++i) {
            const Sexpr &item = alist[i];
            if (only_variables) {
                if (!item.is_atom() || !starts_with_qmark(item.atom())) {
                    ctx.error("Expected a variable but the given word does "
                              "not start with '?'.", &item);
                }
            }
            result.push_back(constructor(ctx, item, type_sexpr));
        }
        cursor = next_cursor;
        ++group_number;
    }
    return result;
}

vector<TypedObject> parse_typed_list(
    Context &ctx, const SexprList &alist, bool only_variables = false,
    bool either_allowed = false,
    const string &default_type = "object") {
    return parse_typed_list_typed<TypedObject>(
        ctx, alist, only_variables, either_allowed,
        construct_typed_object, default_type);
}

vector<Type> parse_type_list(Context &ctx, const SexprList &alist) {
    return parse_typed_list_typed<Type>(
        ctx, alist, /*only_variables=*/false, /*either_allowed=*/false,
        construct_type, "object");
}

/* ----------------------------- requirements ------------------------- */

Requirements parse_requirements(Context &ctx, const SexprList &alist) {
    auto layer = ctx.layer("Parsing requirements");
    vector<string> req_strings;
    req_strings.reserve(alist.size());
    for (const auto &item : alist) {
        check_word(ctx, item, "Requirement label");
        req_strings.push_back(item.atom());
    }
    try {
        return Requirements(move(req_strings));
    } catch (const exception &e) {
        ctx.error(string("Error in requirements.\nReason: ") + e.what());
    }
}

/* ----------------------------- predicates --------------------------- */

Predicate parse_predicate(Context &ctx, const SexprList &alist) {
    string name;
    {
        auto l = ctx.layer("Parsing predicate name");
        if (alist.empty()) {
            ctx.error("Predicate name missing", nullptr, SYNTAX_PREDICATE);
        }
        check_word(ctx, alist[0], "Predicate name");
        name = alist[0].atom();
    }
    auto l = ctx.layer("Parsing arguments of predicate '" + name + "'");
    SexprList rest(alist.begin() + 1, alist.end());
    auto args = parse_typed_list(ctx, rest, /*only_variables=*/true,
                                 /*either_allowed=*/true);
    return Predicate(name, move(args));
}

vector<Predicate> parse_predicates(Context &ctx, const SexprList &alist) {
    auto l = ctx.layer("Parsing predicates");
    vector<Predicate> result;
    int no = 1;
    for (const auto &entry : alist) {
        auto pred_layer = ctx.layer("Parsing predicate #" + to_string(no));
        if (!entry.is_list())
            ctx.error("Invalid predicate definition.", &entry,
                      SYNTAX_PREDICATE);
        result.push_back(parse_predicate(ctx, entry.list()));
        ++no;
    }
    return result;
}

Function parse_function(Context &ctx, const Sexpr &alist_sexpr,
                        const Sexpr &type_sexpr) {
    if (!alist_sexpr.is_list() || alist_sexpr.list().empty()) {
        auto l = ctx.layer("Parsing function name");
        ctx.error("Invalid definition of function.", &alist_sexpr,
                  SYNTAX_FUNCTION);
    }
    const SexprList &alist = alist_sexpr.list();
    string name;
    {
        auto l = ctx.layer("Parsing function name");
        check_word(ctx, alist[0], "Function name");
        name = alist[0].atom();
    }
    auto l = ctx.layer("Parsing function '" + name + "'");
    SexprList rest(alist.begin() + 1, alist.end());
    auto args = parse_typed_list(ctx, rest);
    check_word(ctx, type_sexpr, "Function type");
    string type_name = type_sexpr.atom();
    if (type_name != "number") {
        throw ParseError("Error: object fluents not supported\n"
                         "(function " + name + " has type " + type_name + ")");
    }
    return Function(name, move(args), type_name);
}

/* --------------------------- conditions ----------------------------- */

using PredicateMap = unordered_map<string, const Predicate *>;
using TypeMap = unordered_map<string, const Type *>;

pair<string, int> get_predicate_id_and_arity(
    Context &ctx, const string &text, const TypeMap &type_dict,
    const PredicateMap &predicate_dict) {
    auto type_it = type_dict.find(text);
    auto pred_it = predicate_dict.find(text);
    const Type *the_type = (type_it == type_dict.end()) ? nullptr : type_it->second;
    const Predicate *the_pred =
        (pred_it == predicate_dict.end()) ? nullptr : pred_it->second;
    if (!the_type && !the_pred)
        ctx.error("Undeclared predicate", nullptr, text.c_str());
    if (the_pred) {
        if (the_type) {
            print_warning("name clash between type and predicate '" + text +
                          "'.\nInterpreting as predicate in conditions.");
        }
        return {the_pred->name, the_pred->get_arity()};
    }
    return {the_type->get_predicate_name(), 1};
}

// Validate a predicate-name + term-list. Previously this took a
// pre-built `unordered_set<string>` of valid predicate names, but
// callers were rebuilding that 464K-entry set from `predicate_dict`
// for every literal -- the dominant parse-time cost on pre-grounded
// large domains like trucks-strips/p29. We just consult
// `predicate_dict` directly now.
void check_predicate_and_terms_existence(
    Context &ctx, const string &predicate_name,
    const SexprList &terms,
    const PredicateMap &predicate_dict,
    const unordered_set<string> &valid_term_names) {
    if (!predicate_dict.contains(predicate_name))
        ctx.error("Undefined predicate", nullptr, predicate_name.c_str());
    for (const auto &term : terms) {
        if (!term.is_atom())
            ctx.error("Argument must be a word.", &term);
        const string &t = term.atom();
        if (!valid_term_names.contains(t)) {
            const char *kind = (starts_with_qmark(t)) ? "variable" : "object";
            ctx.error(string("Undefined ") + kind, nullptr, t.c_str());
        }
    }
}

ConditionPtr parse_literal(
    Context &ctx, const SexprList &alist, const TypeMap &type_dict,
    const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names,
    bool negated = false) {
    auto l = ctx.layer("Parsing literal");
    if (alist.empty()) {
        Sexpr empty(SexprList{});
        ctx.error("Literal definition has to be a non-empty block.", &empty,
                  "(PREDICATE ARGUMENTS*) or (not (PREDICATE ARGUMENTS*))");
    }
    SexprList current = alist;
    if (current[0].is_atom() && current[0].atom() == "not") {
        if (current.size() != 2) {
            Sexpr e(current);
            ctx.error("Negated literal definition has to have exactly one "
                      "block as argument.", &e, SYNTAX_LITERAL_NEGATED);
        }
        if (!current[1].is_list() || current[1].list().empty()) {
            ctx.error("Definition of negated literal has to be a non-empty "
                      "block.", &current[1], SYNTAX_LITERAL);
        }
        current = current[1].list();
        negated = !negated;
    }
    if (!current[0].is_atom())
        ctx.error("Predicate name must be a word.", &current[0]);
    string predicate_name = current[0].atom();
    SexprList terms(current.begin() + 1, current.end());

    check_predicate_and_terms_existence(ctx, predicate_name, terms,
                                        predicate_dict, term_names);
    auto [pred_id, arity] =
        get_predicate_id_and_arity(ctx, predicate_name, type_dict,
                                   predicate_dict);
    int got_arity = static_cast<int>(terms.size());
    if (arity != got_arity) {
        Sexpr e(current);
        ctx.error("Predicate '" + predicate_name + "' of arity " +
                  to_string(arity) + " used with " +
                  to_string(got_arity) + " arguments.", &e);
    }
    vector<string> arg_names;
    arg_names.reserve(terms.size());
    for (const auto &t : terms) arg_names.push_back(t.atom());
    if (negated)
        return make_shared<NegatedAtom>(pred_id, move(arg_names));
    return make_shared<Atom>(pred_id, move(arg_names));
}

ConditionPtr parse_condition_aux(
    Context &ctx, const Sexpr &alist_sexpr, bool negated,
    const TypeMap &type_dict, const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names);

ConditionPtr parse_condition(
    Context &ctx, const Sexpr &alist_sexpr, const TypeMap &type_dict,
    const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names) {
    auto l = ctx.layer("Parsing condition");
    ConditionPtr condition = parse_condition_aux(
        ctx, alist_sexpr, false, type_dict, predicate_dict, term_names);
    unordered_map<string, string> type_map;
    unordered_map<string, string> renamings;
    return condition->uniquify_variables(type_map, renamings)->simplified();
}

ConditionPtr parse_condition_aux(
    Context &ctx, const Sexpr &alist_sexpr, bool negated,
    const TypeMap &type_dict, const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names) {
    if (!alist_sexpr.is_list()) {
        ctx.error("Expected a condition block.", &alist_sexpr);
    }
    const SexprList &alist = alist_sexpr.list();
    if (alist.empty()) {
        return make_shared<Conjunction>(vector<ConditionPtr>{});
    }
    if (!alist[0].is_atom()) {
        ctx.error("Expected logical operator or predicate name", &alist[0]);
    }
    const string &tag = alist[0].atom();
    vector<TypedObject> parameters;
    SexprList args;
    if (tag == "and" || tag == "or" || tag == "not" || tag == "imply") {
        args = SexprList(alist.begin() + 1, alist.end());
        if (tag == "imply" && args.size() != 2) {
            ctx.error("'imply' expects exactly two arguments.", nullptr,
                      SYNTAX_CONDITION_IMPLY);
        }
        if (tag == "not") {
            if (args.size() != 1) {
                ctx.error("'not' expects exactly one argument.", nullptr,
                          SYNTAX_CONDITION_NOT);
            }
            negated = !negated;
        }
    } else if (tag == "forall" || tag == "exists") {
        if (alist.size() != 3) {
            ctx.error("'forall' and 'exists' expect exactly two arguments.",
                      nullptr, SYNTAX_CONDITION_FORALL_EXISTS);
        }
        if (!alist[1].is_list() || alist[1].list().empty()) {
            ctx.error("The first argument (VARIABLES) of 'forall' and "
                      "'exists' is expected to be a non-empty block.",
                      nullptr, SYNTAX_CONDITION_FORALL_EXISTS);
        }
        parameters = parse_typed_list(ctx, alist[1].list());
        args.push_back(alist[2]);
    } else if (predicate_dict.contains(tag) ||
               type_dict.contains(tag)) {
        return parse_literal(ctx, alist, type_dict, predicate_dict,
                             term_names, negated);
    } else {
        ctx.error("Expected logical operator or predicate name", &alist[0]);
    }

    for (size_t k = 0; k < args.size(); ++k) {
        if (!args[k].is_list() || args[k].list().empty()) {
            const char *syntax = condition_tag_to_syntax(tag);
            ctx.error("'" + tag + "' expects as argument #" +
                      to_string(k + 1) + " a non-empty block.",
                      &args[k], syntax);
        }
    }

    vector<ConditionPtr> parts;
    string effective_tag = tag;
    if (tag == "imply") {
        parts.push_back(parse_condition_aux(ctx, args[0], !negated,
                                            type_dict, predicate_dict,
                                            term_names));
        parts.push_back(parse_condition_aux(ctx, args[1], negated, type_dict,
                                            predicate_dict, term_names));
        effective_tag = "or";
    } else {
        unordered_set<string> new_term_names = term_names;
        if (tag == "forall" || tag == "exists")
            for (const auto &p : parameters)
                new_term_names.insert(p.name);
        for (const auto &part : args) {
            parts.push_back(parse_condition_aux(ctx, part, negated, type_dict,
                                                predicate_dict,
                                                new_term_names));
        }
    }

    if ((effective_tag == "and" && !negated) ||
        (effective_tag == "or" && negated)) {
        return make_shared<Conjunction>(move(parts));
    }
    if ((effective_tag == "or" && !negated) ||
        (effective_tag == "and" && negated)) {
        return make_shared<Disjunction>(move(parts));
    }
    if ((effective_tag == "forall" && !negated) ||
        (effective_tag == "exists" && negated)) {
        return make_shared<UniversalCondition>(move(parameters),
                                                    move(parts));
    }
    if ((effective_tag == "exists" && !negated) ||
        (effective_tag == "forall" && negated)) {
        return make_shared<ExistentialCondition>(move(parameters),
                                                      move(parts));
    }
    // effective_tag == "not"
    return parts[0];
}

/* ----------------------------- expressions -------------------------- */

bool is_nonnegative_int_literal(const string &s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool is_decimal_literal(const string &s) {
    bool seen_dot = false;
    for (char c : s) {
        if (c == '.') {
            if (seen_dot) return false;
            seen_dot = true;
        } else if (c < '0' || c > '9') {
            return false;
        }
    }
    return seen_dot;
}

pddl::FExprPtr parse_expression(Context &ctx, const Sexpr &exp) {
    auto l = ctx.layer("Parsing expression");
    if (exp.is_list()) {
        const SexprList &lst = exp.list();
        if (lst.empty())
            ctx.error("Expression cannot be an empty block.", &exp,
                      SYNTAX_EXPRESSION);
        check_word(ctx, lst[0], "Function symbol");
        vector<string> args;
        args.reserve(lst.size() - 1);
        for (size_t i = 1; i < lst.size(); ++i)
            args.push_back(lst[i].atom());
        return make_shared<pddl::PrimitiveNumericExpression>(
            lst[0].atom(), move(args));
    }
    const string &s = exp.atom();
    if (s.size() >= 1 && s[0] == '-')
        ctx.error("Negative numbers are not allowed.", &exp, SYNTAX_EXPRESSION);
    if (is_nonnegative_int_literal(s))
        return make_shared<pddl::NumericConstant>(stoll(s));
    if (is_decimal_literal(s))
        ctx.error("Fractional numbers are not supported.", &exp,
                  SYNTAX_EXPRESSION);
    return make_shared<pddl::PrimitiveNumericExpression>(
        s, vector<string>{});
}

shared_ptr<pddl::FunctionAssignment> parse_assignment(
    Context &ctx, const SexprList &alist) {
    auto l = ctx.layer("Parsing Assignment");
    if (alist.size() != 3)
        ctx.error("Assignment expects two arguments", nullptr,
                  SYNTAX_ASSIGNMENT);
    const string &op = alist[0].atom();
    auto head = parse_expression(ctx, alist[1]);
    auto exp = parse_expression(ctx, alist[2]);
    if (head->kind() != pddl::FunctionalExpression::Kind::PNE) {
        ctx.error("Left-hand side of assignment must be a function "
                  "expression.", &alist[1]);
    }
    auto pne_head = const_pointer_cast<pddl::PrimitiveNumericExpression>(
        static_pointer_cast<const pddl::PrimitiveNumericExpression>(head));
    if (op == "=") {
        return make_shared<pddl::Assign>(pne_head, exp);
    } else if (op == "increase") {
        return make_shared<pddl::Increase>(pne_head, exp);
    }
    ctx.error("Unsupported assignment operator '" + op +
              "'. Use '=' or 'increase'.");
}

/* ------------------------------- effects ---------------------------- */

pddl::AnyEffectPtr parse_effect_tree(
    Context &ctx, const Sexpr &alist_sexpr,
    const TypeMap &type_dict, const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names) {
    if (!alist_sexpr.is_list() || alist_sexpr.list().empty()) {
        ctx.error("All (sub-)effects have to be a non-empty blocks.",
                  &alist_sexpr);
    }
    const SexprList &alist = alist_sexpr.list();
    if (!alist[0].is_atom()) {
        ctx.error("Effect head must be a word.", &alist[0]);
    }
    const string &tag = alist[0].atom();
    if (tag == "and") {
        vector<pddl::AnyEffectPtr> effects;
        for (size_t i = 1; i < alist.size(); ++i) {
            check_list(ctx, alist[i], "Each sub-effect of a conjunction");
            effects.push_back(parse_effect_tree(ctx, alist[i], type_dict,
                                                predicate_dict, term_names));
        }
        return make_shared<pddl::ConjunctiveEffect>(move(effects));
    }
    if (tag == "forall") {
        if (alist.size() != 3)
            ctx.error("'forall' effect expects exactly two arguments.",
                      nullptr, SYNTAX_EFFECT_FORALL);
        check_list(ctx, alist[1],
                   "First argument (VARIABLES) of 'forall'",
                   SYNTAX_EFFECT_FORALL);
        auto parameters = parse_typed_list(ctx, alist[1].list());
        check_list(ctx, alist[2], "Second argument (EFFECT) of 'forall'",
                   SYNTAX_EFFECT_FORALL);
        unordered_set<string> nested = term_names;
        for (const auto &p : parameters)
            nested.insert(p.name);
        auto eff = parse_effect_tree(ctx, alist[2], type_dict, predicate_dict,
                                     nested);
        return make_shared<pddl::UniversalEffect>(move(parameters),
                                                       move(eff));
    }
    if (tag == "when") {
        if (alist.size() != 3)
            ctx.error("'when' effect expects exactly two arguments.", nullptr,
                      SYNTAX_EFFECT_WHEN);
        check_list(ctx, alist[1], "First argument (CONDITION) of 'when'",
                   SYNTAX_EFFECT_WHEN);
        auto condition = parse_condition(ctx, alist[1], type_dict,
                                         predicate_dict, term_names);
        check_list(ctx, alist[2], "Second argument (EFFECT) of 'when'",
                   SYNTAX_EFFECT_WHEN);
        auto effect = parse_effect_tree(ctx, alist[2], type_dict,
                                        predicate_dict, term_names);
        return make_shared<pddl::ConditionalEffect>(move(condition),
                                                         move(effect));
    }
    if (tag == "increase") {
        if (alist.size() != 3 ||
            !alist[1].is_list() || alist[1].list().size() != 1 ||
            !alist[1].list()[0].is_atom() ||
            alist[1].list()[0].atom() != "total-cost") {
            ctx.error("'increase' expects two arguments", &alist_sexpr,
                      SYNTAX_EFFECT_INCREASE);
        }
        auto assignment = parse_assignment(ctx, alist);
        // CostEffect wraps an Increase.
        auto incr = dynamic_pointer_cast<Increase>(assignment);
        if (!incr) {
            ctx.error("'increase' assignment expected.");
        }
        return make_shared<pddl::CostEffect>(move(incr));
    }
    // Simple effect.
    TypeMap empty_types;
    auto lit = parse_literal(ctx, alist, empty_types, predicate_dict,
                             term_names);
    return make_shared<pddl::SimpleEffect>(move(lit));
}

bool effect_in_result(const Effect &target, const vector<Effect> &result) {
    for (const auto &e : result)
        if (e.equals(target)) return true;
    return false;
}

void add_effect(const pddl::AnyEffectPtr &tmp_effect,
                vector<Effect> &result) {
    if (!tmp_effect) return;
    if (tmp_effect->kind() == pddl::AnyEffect::Kind::CONJUNCTIVE) {
        const auto &c = static_cast<pddl::ConjunctiveEffect &>(*tmp_effect);
        for (const auto &e : c.effects)
            add_effect(e, result);
        return;
    }
    vector<TypedObject> parameters;
    ConditionPtr condition = make_shared<pddl::Truth>();
    ConditionPtr literal;
    if (tmp_effect->kind() == pddl::AnyEffect::Kind::UNIVERSAL) {
        const auto &u = static_cast<pddl::UniversalEffect &>(*tmp_effect);
        parameters = u.parameters;
        if (u.effect &&
            u.effect->kind() == pddl::AnyEffect::Kind::CONDITIONAL) {
            const auto &ce =
                static_cast<pddl::ConditionalEffect &>(*u.effect);
            condition = ce.condition;
            const auto &se = static_cast<pddl::SimpleEffect &>(*ce.effect);
            literal = se.literal;
        } else {
            const auto &se = static_cast<pddl::SimpleEffect &>(*u.effect);
            literal = se.literal;
        }
    } else if (tmp_effect->kind() == pddl::AnyEffect::Kind::CONDITIONAL) {
        const auto &ce = static_cast<pddl::ConditionalEffect &>(*tmp_effect);
        condition = ce.condition;
        const auto &se = static_cast<pddl::SimpleEffect &>(*ce.effect);
        literal = se.literal;
    } else {
        const auto &se = static_cast<pddl::SimpleEffect &>(*tmp_effect);
        literal = se.literal;
    }
    condition = condition->simplified();
    Effect new_effect(parameters, condition, literal);
    Effect contradiction(parameters, condition, literal->negate());
    if (!effect_in_result(contradiction, result)) {
        result.push_back(move(new_effect));
    } else {
        // Add-after-delete semantics: prefer the positive effect.
        const auto &lit = static_cast<const pddl::Literal &>(*literal);
        if (lit.negated()) {
            // The new effect is negative, the existing positive one wins.
            return;
        }
        // The new effect is positive; remove the existing negative.
        auto it = ranges::find_if(result,
                               [&](const Effect &e) {
                                   return e.equals(contradiction);
                               });
        if (it != result.end()) result.erase(it);
        result.push_back(move(new_effect));
    }
}

shared_ptr<Increase> parse_effects(
    Context &ctx, const Sexpr &alist_sexpr,
    vector<Effect> &result,
    const TypeMap &type_dict, const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names) {
    auto l = ctx.layer("Parsing effect");
    auto tmp = parse_effect_tree(ctx, alist_sexpr, type_dict, predicate_dict,
                                 term_names);
    auto normalized = tmp->normalize();
    auto [cost_eff, rest_effect] = pddl::extract_cost(normalized);
    add_effect(rest_effect, result);
    if (cost_eff && cost_eff->effect) return cost_eff->effect;
    return nullptr;
}

/* ------------------------------- actions ---------------------------- */

optional<Action> parse_action(
    Context &ctx, const SexprList &alist, const TypeMap &type_dict,
    const PredicateMap &predicate_dict,
    unordered_set<string> &constant_names) {
    string name;
    {
        auto l = ctx.layer("Parsing action name");
        if (alist.size() < 4) {
            Sexpr s(alist);
            ctx.error("Expecting block with at least 3 arguments for an "
                      "action.", &s, SYNTAX_ACTION);
        }
        if (!alist[0].is_atom() || alist[0].atom() != ":action") {
            ctx.error("Action tag must be ':action'.", &alist[0]);
        }
        check_word(ctx, alist[1], "Action name", SYNTAX_ACTION);
        name = alist[1].atom();
    }
    auto outer = ctx.layer("Parsing action '" + name + "'");
    size_t idx = 2;
    vector<TypedObject> parameters;
    ConditionPtr precondition;
    vector<Effect> effects;
    shared_ptr<Increase> cost;

    {
        auto l = ctx.layer("Parsing parameters");
        if (idx >= alist.size())
            ctx.error("Missing fields. Expecting " + string(SYNTAX_ACTION));
        if (alist[idx].is_atom() && alist[idx].atom() == ":parameters") {
            ++idx;
            if (idx >= alist.size())
                ctx.error("Missing parameters list.", nullptr, SYNTAX_ACTION);
            check_list(ctx, alist[idx], "Parameters", SYNTAX_ACTION);
            parameters = parse_typed_list(ctx, alist[idx].list(),
                                          /*only_variables=*/true);
            ++idx;
        }
    }
    /*
      Use the caller's `constant_names` set as the term-name scope and
      push the action's parameters into it for the duration of parsing
      the precondition/effect, then erase them. This avoids copying
      the full constant set per action, which was the dominant cost on
      pre-grounded large domains (trucks-strips/p29 has 56 770 actions
      and a 12 MB domain file -- the wholesale copy was ~27 % of
      runtime per perf record).
    */
    unordered_set<string> &term_names = constant_names;
    vector<string> pushed_params;
    pushed_params.reserve(parameters.size());
    for (const auto &p : parameters) {
        if (term_names.insert(p.name).second)
            pushed_params.push_back(p.name);
    }
    auto pop_params = [&]() {
        for (const auto &n : pushed_params) term_names.erase(n);
    };
    {
        auto l = ctx.layer("Parsing precondition");
        if (idx >= alist.size()) {
            pop_params();
            ctx.error("Missing fields. Expecting " + string(SYNTAX_ACTION));
        }
        if (alist[idx].is_atom() && alist[idx].atom() == ":precondition") {
            ++idx;
            if (idx >= alist.size()) {
                pop_params();
                ctx.error("Missing precondition.", nullptr, SYNTAX_ACTION);
            }
            check_list(ctx, alist[idx], "Precondition", SYNTAX_ACTION);
            precondition = parse_condition(ctx, alist[idx], type_dict,
                                           predicate_dict, term_names);
            ++idx;
        } else {
            precondition = make_shared<Conjunction>(
                vector<ConditionPtr>{});
        }
    }
    {
        auto l = ctx.layer("Parsing effect");
        if (idx >= alist.size()) {
            pop_params();
            ctx.error("Missing fields. Expecting " + string(SYNTAX_ACTION));
        }
        if (!alist[idx].is_atom() || alist[idx].atom() != ":effect") {
            pop_params();
            ctx.error("Effect tag is expected to be ':effect'", &alist[idx],
                      SYNTAX_ACTION);
        }
        ++idx;
        if (idx >= alist.size()) {
            pop_params();
            ctx.error("Missing effect.", nullptr, SYNTAX_ACTION);
        }
        check_list(ctx, alist[idx], "Effect", SYNTAX_ACTION);
        if (!alist[idx].list().empty()) {
            cost = parse_effects(ctx, alist[idx], effects, type_dict,
                                 predicate_dict, term_names);
        }
        ++idx;
    }
    pop_params();
    if (idx != alist.size())
        ctx.error("Too many fields. Expecting " + string(SYNTAX_ACTION));
    if (!effects.empty() || get_options().keep_no_ops) {
        int n = static_cast<int>(parameters.size());
        return Action(name, move(parameters), n, move(precondition),
                      move(effects), move(cost));
    }
    return nullopt;
}

Axiom parse_axiom(Context &ctx, const SexprList &alist,
                  const TypeMap &type_dict, const PredicateMap &predicate_dict,
                  unordered_set<string> &constant_names) {
    Predicate predicate;
    {
        auto l = ctx.layer("Parsing derived predicate");
        if (alist.size() != 3) {
            Sexpr s(alist);
            ctx.error("Expecting block with exactly three elements", &s,
                      SYNTAX_AXIOM);
        }
        check_list(ctx, alist[1], "The first argument (PREDICATE)",
                   SYNTAX_AXIOM);
        predicate = parse_predicate(ctx, alist[1].list());
    }
    auto l = ctx.layer("Parsing condition for derived predicate '" +
                       predicate.name + "'");
    if (!alist[2].is_list()) {
        ctx.error("The second argument (CONDITION) is expected to be a "
                  "block.", nullptr, SYNTAX_AXIOM);
    }
    // Same push/pop trick as parse_action -- avoid copying the
    // potentially-large constant_names set per axiom.
    unordered_set<string> &term_names = constant_names;
    vector<string> pushed;
    pushed.reserve(predicate.arguments.size());
    for (const auto &a : predicate.arguments) {
        if (term_names.insert(a.name).second) pushed.push_back(a.name);
    }
    auto condition = parse_condition(ctx, alist[2], type_dict, predicate_dict,
                                     term_names);
    for (const auto &n : pushed) term_names.erase(n);
    int arity = static_cast<int>(predicate.arguments.size());
    return Axiom(predicate.name, move(predicate.arguments), arity,
                 move(condition));
}

void parse_axioms_and_actions(Context &ctx, const vector<Sexpr> &entries,
                              const TypeMap &type_dict,
                              const PredicateMap &predicate_dict,
                              unordered_set<string> &constant_names,
                              vector<Axiom> &axioms,
                              vector<Action> &actions) {
    int no = 1;
    for (const auto &entry : entries) {
        auto l = ctx.layer("Parsing axiom/action entry #" + to_string(no));
        check_named_block(ctx, entry, {":derived", ":action"});
        const string &head = entry.list()[0].atom();
        if (head == ":derived") {
            auto layer2 = ctx.layer(
                "Parsing " + to_string(axioms.size() + 1) + ". axiom");
            axioms.push_back(parse_axiom(ctx, entry.list(), type_dict,
                                         predicate_dict, constant_names));
        } else {
            auto layer2 = ctx.layer(
                "Parsing action #" + to_string(actions.size() + 1));
            auto action = parse_action(ctx, entry.list(), type_dict,
                                       predicate_dict, constant_names);
            if (action) actions.push_back(move(*action));
        }
        ++no;
    }
}

/* ------------------------------ init -------------------------------- */

void check_atom_consistency(
    Context &ctx, const Atom &atom,
    unordered_map<shared_ptr<const Atom>, bool, pddl::ConditionPtrHash,
                       pddl::ConditionPtrEqual> &values,
    bool value) {
    auto key = make_shared<const Atom>(atom);
    auto it = values.find(key);
    if (it != values.end()) {
        bool prev = it->second;
        if (prev != value) {
            ostringstream os;
            atom.dump(os, 0);
            ctx.error("Error in initial state specification\nReason: " +
                      os.str() + " is true and false.");
        } else {
            ostringstream os;
            atom.dump(os, 0);
            if (!value) os << "(negated)";
            print_warning(os.str() +
                          " is specified twice in initial state specification");
        }
    }
}

vector<pddl::InitElement> parse_init(
    Context &ctx, const SexprList &alist, const PredicateMap &predicate_dict,
    const unordered_set<string> &term_names) {
    vector<pddl::InitElement> initial;
    unordered_map<shared_ptr<pddl::PrimitiveNumericExpression>,
                       shared_ptr<pddl::Assign>>
        initial_assignments;
    unordered_map<shared_ptr<const Atom>, bool,
                       pddl::ConditionPtrHash, pddl::ConditionPtrEqual>
        initial_proposition_values;

    for (size_t k = 1; k < alist.size(); ++k) {
        auto l = ctx.layer("Parsing element #" + to_string(k) +
                           " in init block");
        const Sexpr &fact = alist[k];
        if (!fact.is_list() || fact.list().empty()) {
            ctx.error("Invalid fact.", &fact,
                      "(PREDICATE ARGUMENTS*) or (not (...)) or "
                      "({=,increase} EXPRESSION EXPRESSION)");
        }
        const SexprList &flist = fact.list();
        if (flist[0].is_atom() && flist[0].atom() == "=") {
            auto assignment = parse_assignment(ctx, flist);
            auto assign = dynamic_pointer_cast<pddl::Assign>(assignment);
            if (!assign) {
                ctx.error("Initial state assignment must use '='.");
            }
            if (assign->expression->kind() !=
                pddl::FunctionalExpression::Kind::CONSTANT) {
                ctx.error("Illegal assignment in initial state specification.");
            }
            // Look up by fluent value (PNE equality).
            bool merged = false;
            for (auto &[fl, prev] : initial_assignments) {
                if (*fl == *assign->fluent) {
                    auto prev_const =
                        static_pointer_cast<const pddl::NumericConstant>(
                            prev->expression);
                    auto new_const =
                        static_pointer_cast<const pddl::NumericConstant>(
                            assign->expression);
                    if (prev_const->value == new_const->value) {
                        print_warning("assignment specified twice in initial "
                                      "state specification");
                    } else {
                        ctx.error("Error in initial state specification\n"
                                  "Reason: conflicting assignment for fluent.");
                    }
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                initial_assignments[assign->fluent] = assign;
                initial.emplace_back(assign);
            }
            continue;
        }
        bool atom_value = true;
        SexprList atom_list = flist;
        if (flist[0].is_atom() && flist[0].atom() == "not") {
            atom_value = false;
            if (flist.size() != 2)
                ctx.error("Expecting " + string(SYNTAX_LITERAL_NEGATED) +
                          " for negated atoms.");
            if (!flist[1].is_list() || flist[1].list().empty())
                ctx.error("Invalid negated fact.", nullptr,
                          SYNTAX_LITERAL_NEGATED);
            atom_list = flist[1].list();
        }
        const string &pname = atom_list[0].atom();
        SexprList terms(atom_list.begin() + 1, atom_list.end());
        check_predicate_and_terms_existence(ctx, pname, terms,
                                            predicate_dict, term_names);
        auto pred_it = predicate_dict.find(pname);
        int expected_arity = pred_it->second->get_arity();
        int got_arity = static_cast<int>(terms.size());
        if (expected_arity != got_arity) {
            Sexpr e(atom_list);
            ctx.error("Predicate '" + pname + "' of arity " +
                      to_string(expected_arity) + " used with " +
                      to_string(got_arity) + " arguments.", &e);
        }
        vector<string> arg_names;
        arg_names.reserve(terms.size());
        for (const auto &t : terms) arg_names.push_back(t.atom());
        Atom atom(pname, move(arg_names));
        check_atom_consistency(ctx, atom, initial_proposition_values,
                               atom_value);
        auto atom_ptr = make_shared<const Atom>(move(atom));
        initial_proposition_values[atom_ptr] = atom_value;
    }
    for (auto &[atom, val] : initial_proposition_values) {
        if (val) initial.emplace_back(atom);
    }
    return initial;
}

/* --------------------------- task aggregation ----------------------- */

void check_for_duplicates(Context &ctx,
                          const vector<string> &elements,
                          const string &element_type) {
    set<string> seen, duplicates;
    for (const auto &el : elements) {
        if (!seen.insert(el).second) duplicates.insert(el);
    }
    if (!duplicates.empty()) {
        string msg = "Found the following duplicate " + element_type +
                          "s: ";
        bool first = true;
        for (const auto &d : duplicates) {
            msg += (first ? "" : ", ") + d;
            first = false;
        }
        if (element_type == "action") {
            print_warning(msg);
        } else {
            ctx.error(msg);
        }
    }
}

void set_supertypes(vector<Type> &types) {
    unordered_map<string, size_t> idx;
    vector<pair<string, string>> child_types;
    for (size_t i = 0; i < types.size(); ++i) {
        types[i].supertype_names.clear();
        idx[types[i].name] = i;
        if (types[i].basetype_name && !types[i].basetype_name->empty())
            child_types.emplace_back(types[i].name, *types[i].basetype_name);
    }
    for (const auto &[desc, anc] : utils::transitive_closure(child_types)) {
        auto it = idx.find(desc);
        if (it != idx.end())
            types[it->second].supertype_names.push_back(anc);
    }
}

struct DomainPart {
    string domain_name;
    Requirements requirements;
    vector<Type> types;
    vector<TypedObject> constants;
    vector<Predicate> predicates;
    vector<Function> functions;
    vector<Action> actions;
    vector<Axiom> axioms;
};

DomainPart parse_domain_pddl(Context &ctx, const Sexpr &domain_pddl) {
    if (!domain_pddl.is_list() || domain_pddl.list().size() < 2)
        ctx.error("The domain file must start with the define keyword and "
                  "the domain name.");
    const SexprList &top = domain_pddl.list();
    DomainPart out;
    auto l = ctx.layer("Parsing domain");
    if (!top[0].is_atom() || top[0].atom() != "define") {
        ctx.error("Domain definition expected to start with '(define '.");
    }
    {
        auto dl = ctx.layer("Parsing domain name");
        const Sexpr &dline = top[1];
        check_named_block(ctx, dline, {"domain"}, SYNTAX_DOMAIN_DOMAIN_NAME);
        if (dline.list().size() != 2 || !dline.list()[1].is_atom())
            ctx.error("The definition of the domain name expects exactly one "
                      "word after 'domain'.", &dline,
                      SYNTAX_DOMAIN_DOMAIN_NAME);
        out.domain_name = dline.list()[1].atom();
    }
    // Default values matching Python.
    out.requirements = Requirements({":strips"});
    out.types.emplace_back("object");

    const vector<string> correct_order = {
        ":requirements", ":types", ":constants", ":predicates", ":functions"};
    const vector<string> action_axiom = {":derived", ":action"};
    vector<string> seen_fields;
    size_t idx = 2;
    vector<Sexpr> entries; // saved action/axiom-style entries
    bool first_action_seen = false;
    for (; idx < top.size(); ++idx) {
        vector<string> allowed = correct_order;
        for (const auto &n : action_axiom) allowed.push_back(n);
        check_named_block(ctx, top[idx], allowed);
        const string &field = top[idx].list()[0].atom();
        if (ranges::find(correct_order, field) ==
            correct_order.end()) {
            entries.push_back(top[idx]);
            first_action_seen = true;
            ++idx;
            break;
        }
        if (ranges::find(seen_fields, field) !=
            seen_fields.end()) {
            ctx.error("Error in domain specification\nReason: two '" + field +
                      "' specifications.");
        }
        if (!seen_fields.empty()) {
            auto a = ranges::find(correct_order,
                               seen_fields.back());
            auto b = ranges::find(correct_order,
                               field);
            if (a > b) {
                print_warning(field +
                              " specification not allowed here (cf. PDDL BNF)");
            }
        }
        seen_fields.push_back(field);
        const SexprList &block = top[idx].list();
        SexprList body(block.begin() + 1, block.end());
        if (field == ":requirements") {
            out.requirements = parse_requirements(ctx, body);
        } else if (field == ":types") {
            auto tl = ctx.layer("Parsing types");
            auto more = parse_type_list(ctx, body);
            for (auto &t : more) {
                if (t.name == "number") {
                    ctx.error("Encountered declaration of type \"number\", "
                              "which is a reserved type that cannot be "
                              "redeclared.");
                }
                out.types.push_back(move(t));
            }
        } else if (field == ":constants") {
            auto cl = ctx.layer("Parsing constants");
            out.constants = parse_typed_list(ctx, body);
        } else if (field == ":predicates") {
            out.predicates = parse_predicates(ctx, body);
            out.predicates.push_back(Predicate(
                "=",
                {TypedObject("?x", "object"), TypedObject("?y", "object")}));
        } else if (field == ":functions") {
            auto fl = ctx.layer("Parsing functions");
            // Parse with default type "number". Functions use a list-form
            // constructor: each item is itself a list.
            auto wrap = [](Context &c, const Sexpr &name, const Sexpr &type) {
                return parse_function(c, name, type);
            };
            out.functions = parse_typed_list_typed<Function>(
                ctx, body, /*only_variables=*/false,
                /*either_allowed=*/false, wrap, "number");
        }
    }
    if (first_action_seen) {
        for (; idx < top.size(); ++idx)
            entries.push_back(top[idx]);
    }
    set_supertypes(out.types);

    TypeMap type_dict;
    for (const auto &t : out.types) type_dict[t.name] = &t;
    PredicateMap predicate_dict;
    for (const auto &p : out.predicates) predicate_dict[p.name] = &p;
    unordered_set<string> constant_names;
    for (const auto &c : out.constants) constant_names.insert(c.name);
    parse_axioms_and_actions(ctx, entries, type_dict, predicate_dict,
                             constant_names, out.axioms, out.actions);
    return out;
}

struct TaskPart {
    string task_name;
    string task_domain_name;
    Requirements task_requirements;
    vector<TypedObject> objects;
    vector<pddl::InitElement> init;
    ConditionPtr goal;
    bool use_metric = false;
};

TaskPart parse_task_pddl(Context &ctx, const Sexpr &task_pddl,
                        const TypeMap &type_dict,
                        const PredicateMap &predicate_dict,
                        const unordered_set<string> &constant_names) {
    TaskPart out;
    auto l = ctx.layer("Parsing task");
    if (!task_pddl.is_list())
        ctx.error("Invalid definition of a PDDL task.");
    const SexprList &top = task_pddl.list();
    size_t i = 0;
    if (i >= top.size() || !top[i].is_atom() || top[i].atom() != "define")
        ctx.error("Task definition expected to start with '(define ");
    ++i;
    {
        auto pl = ctx.layer("Parsing problem name");
        if (i >= top.size())
            ctx.error("Missing problem name.", nullptr,
                      SYNTAX_TASK_PROBLEM_NAME);
        check_named_block(ctx, top[i], {"problem"}, SYNTAX_TASK_PROBLEM_NAME);
        if (top[i].list().size() != 2 || !top[i].list()[1].is_atom())
            ctx.error("The definition of the problem name expects exactly one"
                      " word after 'problem'.", &top[i],
                      SYNTAX_TASK_PROBLEM_NAME);
        out.task_name = top[i].list()[1].atom();
    }
    ++i;
    {
        auto dl = ctx.layer("Parsing domain name");
        if (i >= top.size())
            ctx.error("Missing domain name.", nullptr,
                      SYNTAX_TASK_DOMAIN_NAME);
        check_named_block(ctx, top[i], {":domain"}, SYNTAX_TASK_DOMAIN_NAME);
        if (top[i].list().size() != 2 || !top[i].list()[1].is_atom())
            ctx.error("The definition of the domain name expects exactly one "
                      "word after ':domain'.", &top[i],
                      SYNTAX_TASK_DOMAIN_NAME);
        out.task_domain_name = top[i].list()[1].atom();
    }
    ++i;
    // Optional :requirements
    if (i < top.size()) {
        check_named_block(ctx, top[i],
                          {":requirements", ":objects", ":init"});
    }
    if (i < top.size() && top[i].list()[0].atom() == ":requirements") {
        SexprList body(top[i].list().begin() + 1, top[i].list().end());
        out.task_requirements = parse_requirements(ctx, body);
        ++i;
    } else {
        out.task_requirements = Requirements(vector<string>{});
    }
    // Optional :objects
    if (i < top.size()) {
        check_named_block(ctx, top[i], {":objects", ":init"});
    }
    if (i < top.size() && top[i].list()[0].atom() == ":objects") {
        auto ol = ctx.layer("Parsing objects");
        SexprList body(top[i].list().begin() + 1, top[i].list().end());
        out.objects = parse_typed_list(ctx, body);
        ++i;
    }
    // :init
    if (i >= top.size())
        ctx.error("Missing :init block.");
    check_named_block(ctx, top[i], {":init"});
    unordered_set<string> term_names = constant_names;
    for (const auto &o : out.objects) term_names.insert(o.name);
    out.init = parse_init(ctx, top[i].list(), predicate_dict, term_names);
    ++i;
    // :goal
    if (i >= top.size())
        ctx.error("Missing :goal block.");
    {
        auto gl = ctx.layer("Parsing goal");
        check_named_block(ctx, top[i], {":goal"}, SYNTAX_GOAL);
        if (top[i].list().size() != 2 || !top[i].list()[1].is_list() ||
            top[i].list()[1].list().empty())
            ctx.error("The definition of the goal expects a non-empty list "
                      "after ':goal'.", &top[i], SYNTAX_GOAL);
        out.goal = parse_condition(ctx, top[i].list()[1], type_dict,
                                   predicate_dict, term_names);
    }
    ++i;
    // optional :metric
    if (i < top.size()) {
        const Sexpr &metric = top[i];
        if (!metric.is_list() || metric.list().empty() ||
            !metric.list()[0].is_atom() ||
            metric.list()[0].atom() != ":metric") {
            ctx.error("After the goal nothing is allowed except the definition"
                      " of the total-cost metric.", &metric, SYNTAX_METRIC);
        }
        auto ml = ctx.layer("Parsing metric");
        if (metric.list().size() != 3 ||
            !metric.list()[1].is_atom() ||
            metric.list()[1].atom() != "minimize" ||
            !metric.list()[2].is_list() || metric.list()[2].list().size() != 1 ||
            !metric.list()[2].list()[0].is_atom() ||
            metric.list()[2].list()[0].atom() != "total-cost") {
            ctx.error("Invalid metric definition.", &metric, SYNTAX_METRIC);
        }
        out.use_metric = true;
        ++i;
    }
    if (i < top.size()) {
        ctx.error("After the goal/metric nothing is allowed.", &top[i]);
    }
    return out;
}
}

/* ------------------------------- public ----------------------------- */

pddl::Task parse_task(const Sexpr &domain, const Sexpr &task) {
    Context ctx;
    auto dom = parse_domain_pddl(ctx, domain);

    TypeMap type_dict;
    for (const auto &t : dom.types) type_dict[t.name] = &t;
    PredicateMap predicate_dict;
    for (const auto &p : dom.predicates) predicate_dict[p.name] = &p;
    unordered_set<string> constant_names;
    for (const auto &c : dom.constants) constant_names.insert(c.name);

    auto tp = parse_task_pddl(ctx, task, type_dict, predicate_dict,
                              constant_names);
    if (dom.domain_name != tp.task_domain_name) {
        ctx.error("The domain name specified by the task (" +
                  tp.task_domain_name + ") does not match the name specified "
                  "by the domain file (" + dom.domain_name + ").");
    }

    // Merge requirements (sorted, deduplicated).
    set<string> merged_reqs(dom.requirements.requirements.begin(),
                                      dom.requirements.requirements.end());
    for (const auto &r : tp.task_requirements.requirements)
        merged_reqs.insert(r);
    vector<string> merged_req_list(merged_reqs.begin(),
                                             merged_reqs.end());
    Requirements requirements(move(merged_req_list));

    // Combine constants and objects into one list (constants first).
    vector<TypedObject> objects = move(dom.constants);
    for (auto &o : tp.objects) objects.push_back(move(o));

    // Check for duplicates.
    vector<string> object_names, action_names;
    object_names.reserve(objects.size());
    for (const auto &o : objects) object_names.push_back(o.name);
    check_for_duplicates(ctx, object_names, "object");
    action_names.reserve(dom.actions.size());
    for (const auto &a : dom.actions) action_names.push_back(a.name);
    check_for_duplicates(ctx, action_names, "action");

    // Add equality identities to init.
    for (const auto &o : objects) {
        auto eq = make_shared<const Atom>(
            "=", vector<string>{o.name, o.name});
        tp.init.emplace_back(eq);
    }

    pddl::Task t;
    t.domain_name = move(dom.domain_name);
    t.task_name = move(tp.task_name);
    t.requirements = move(requirements);
    t.types = move(dom.types);
    t.objects = move(objects);
    t.predicates = move(dom.predicates);
    t.functions = move(dom.functions);
    t.init = move(tp.init);
    t.goal = move(tp.goal);
    t.actions = move(dom.actions);
    t.axioms = move(dom.axioms);
    t.use_min_cost_metric = tp.use_metric;
    return t;
}
}
