#include "normalize.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/task.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace translate::normalize {
using namespace pddl;
using TypeMap = std::unordered_map<std::string, std::string>;

namespace {
/*
  Visit every condition slot in the task: action preconditions, effect
  conditions on each effect, axiom conditions, and the goal. For each
  visit, `fn(get_type_map, get_condition, set_condition)` is called.

  IMPORTANT: actions/axioms snapshot is taken at call time. New axioms
  added during iteration (e.g. by remove_universal_quantifiers) are NOT
  visited.
*/
template<class Fn>
void for_each_condition(Task &task, Fn fn) {
    std::size_t n_actions = task.actions.size();
    std::size_t n_axioms = task.axioms.size();
    for (std::size_t i = 0; i < n_actions; ++i) {
        // Precondition.
        fn([&](){ return task.actions[i].type_map; },
           [&](){ return task.actions[i].precondition; },
           [&](ConditionPtr c){ task.actions[i].precondition = std::move(c); });
        // Effect conditions.
        std::size_t n_effects = task.actions[i].effects.size();
        for (std::size_t k = 0; k < n_effects; ++k) {
            fn([&](){ return task.actions[i].type_map; },
               [&](){ return task.actions[i].effects[k].condition; },
               [&](ConditionPtr c){
                   task.actions[i].effects[k].condition = std::move(c);
               });
        }
    }
    for (std::size_t i = 0; i < n_axioms; ++i) {
        fn([&](){ return task.axioms[i].type_map; },
           [&](){ return task.axioms[i].condition; },
           [&](ConditionPtr c){ task.axioms[i].condition = std::move(c); });
    }
    // Goal.
    fn([&]() -> TypeMap {
           // The goal has no type_map field; populate one by walking it.
           TypeMap m;
           if (task.goal) {
               std::unordered_map<std::string, std::string> renamings;
               (void)task.goal->uniquify_variables(m, renamings);
           }
           return m;
       },
       [&](){ return task.goal; },
       [&](ConditionPtr c){ task.goal = std::move(c); });
}

bool is_literal(const Condition &c) {
    return c.kind() == Condition::Kind::ATOM ||
           c.kind() == Condition::Kind::NEGATED_ATOM;
}

/*
  Sorted vector of free variable names.
*/
std::vector<std::string> sorted_free_variables(const Condition &c) {
    auto fv = c.free_variables();
    std::vector<std::string> sorted(fv.begin(), fv.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

/* [1] remove_universal_quantifiers ------------------------------------ */

// Key for memoizing newly created not-axioms. The (condition, params)
// pair maps to an axiom; we re-use axioms when the same condition arises.
struct AxiomKey {
    ConditionPtr condition;
    std::vector<TypedObject> parameters;
};
struct AxiomKeyHash {
    std::size_t operator()(const AxiomKey &k) const noexcept {
        std::size_t h = k.condition ? k.condition->hash() : 0;
        for (const auto &p : k.parameters) {
            std::size_t x = std::hash<std::string>{}(p.name);
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            x = std::hash<std::string>{}(p.type_name);
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return h;
    }
};
struct AxiomKeyEqual {
    bool operator()(const AxiomKey &a, const AxiomKey &b) const {
        ConditionPtrEqual eq;
        if (!eq(a.condition, b.condition)) return false;
        if (a.parameters.size() != b.parameters.size()) return false;
        for (std::size_t i = 0; i < a.parameters.size(); ++i)
            if (a.parameters[i] != b.parameters[i]) return false;
        return true;
    }
};

ConditionPtr remove_universal_recurse(
    Task &task, const TypeMap &type_map,
    std::unordered_map<AxiomKey, std::string, AxiomKeyHash, AxiomKeyEqual>
        &memo,
    const ConditionPtr &condition) {
    if (condition->kind() == Condition::Kind::UNIVERSAL) {
        auto axiom_condition = condition->negate();
        auto params_names = sorted_free_variables(*axiom_condition);
        std::vector<TypedObject> typed_params;
        typed_params.reserve(params_names.size());
        for (const auto &v : params_names) {
            auto it = type_map.find(v);
            std::string tn = (it == type_map.end()) ? "object" : it->second;
            typed_params.emplace_back(v, tn);
        }
        AxiomKey key{axiom_condition, typed_params};
        auto memo_it = memo.find(key);
        // Cache the axiom *name* (a stable std::string), not an Axiom*:
        // task.add_axiom appends to a std::vector<Axiom> and may reallocate,
        // which would dangle any cached Axiom* and corrupt axiom->name.
        std::string axiom_name =
            (memo_it != memo.end()) ? memo_it->second : std::string();
        if (axiom_name.empty()) {
            ConditionPtr inner_processed = remove_universal_recurse(
                task, type_map, memo, axiom_condition);
            std::vector<TypedObject> params_copy = typed_params;
            axiom_name =
                task.add_axiom(std::move(params_copy), inner_processed)->name;
            // Re-key memo entry. Since AxiomKey uses condition+params, it
            // remains stable across vector reallocation.
            memo.emplace(AxiomKey{inner_processed, typed_params}, axiom_name);
            // Use the original key as well so identical condition+params
            // map to the same axiom.
            memo[key] = axiom_name;
        }
        std::vector<std::string> arg_names = params_names;
        return std::make_shared<NegatedAtom>(std::move(axiom_name),
                                             std::move(arg_names));
    }
    // Recurse over children and rebuild via change_parts.
    std::vector<ConditionPtr> new_parts;
    const auto &kids = condition->parts();
    new_parts.reserve(kids.size());
    for (const auto &p : kids)
        new_parts.push_back(remove_universal_recurse(task, type_map, memo, p));
    return condition->change_parts(std::move(new_parts));
}

void remove_universal_quantifiers(Task &task) {
    std::unordered_map<AxiomKey, std::string, AxiomKeyHash, AxiomKeyEqual> memo;
    for_each_condition(task,
        [&](auto get_tm, auto get_c, auto set_c) {
            auto c = get_c();
            if (c && c->has_universal_part()) {
                auto tm = get_tm();
                set_c(remove_universal_recurse(task, tm, memo, c));
            }
        });
}

/* [2] substitute_complicated_goal ------------------------------------- */

void substitute_complicated_goal(Task &task) {
    if (!task.goal) return;
    const Condition &g = *task.goal;
    if (is_literal(g)) return;
    if (g.kind() == Condition::Kind::CONJUNCTION) {
        bool all_literals = true;
        for (const auto &p : g.parts())
            if (!p || !is_literal(*p)) { all_literals = false; break; }
        if (all_literals) return;
    }
    auto new_axiom = task.add_axiom({}, task.goal);
    task.goal = std::make_shared<Atom>(new_axiom->name,
                                       std::vector<std::string>{});
}

/* [3] build_DNF ------------------------------------------------------- */

ConditionPtr build_dnf_recurse(const ConditionPtr &condition) {
    std::vector<ConditionPtr> disjunctive;
    std::vector<ConditionPtr> other;
    for (const auto &part : condition->parts()) {
        auto p = build_dnf_recurse(part);
        if (p->kind() == Condition::Kind::DISJUNCTION) {
            disjunctive.push_back(std::move(p));
        } else {
            other.push_back(std::move(p));
        }
    }
    if (disjunctive.empty()) return condition;

    if (condition->kind() == Condition::Kind::DISJUNCTION) {
        std::vector<ConditionPtr> result = other;
        for (const auto &d : disjunctive) {
            for (const auto &q : d->parts()) result.push_back(q);
        }
        return std::make_shared<Disjunction>(std::move(result));
    }
    if (condition->kind() == Condition::Kind::EXISTENTIAL) {
        const auto &q = static_cast<const ExistentialCondition &>(*condition);
        std::vector<ConditionPtr> result;
        for (const auto &part : disjunctive[0]->parts())
            result.push_back(std::make_shared<ExistentialCondition>(
                q.parameters, std::vector<ConditionPtr>{part}));
        return std::make_shared<Disjunction>(std::move(result));
    }
    // Conjunction case: distribute.
    std::vector<ConditionPtr> result_parts = {
        std::make_shared<Conjunction>(other)};
    while (!disjunctive.empty()) {
        auto prev = std::move(result_parts);
        result_parts.clear();
        auto to_distribute = disjunctive.back();
        disjunctive.pop_back();
        for (const auto &p1 : prev) {
            for (const auto &p2 : to_distribute->parts()) {
                std::vector<ConditionPtr> conj = {p1, p2};
                result_parts.push_back(
                    std::make_shared<Conjunction>(std::move(conj)));
            }
        }
    }
    return std::make_shared<Disjunction>(std::move(result_parts));
}

void build_DNF(Task &task) {
    for_each_condition(task,
        [&](auto /*get_tm*/, auto get_c, auto set_c) {
            auto c = get_c();
            if (c && c->has_disjunction())
                set_c(build_dnf_recurse(c)->simplified());
        });
}

/* [4] split_disjunctions --------------------------------------------- */

/*
  For each disjunction at the root of a precondition/effect-condition/
  axiom-condition, duplicate the owner once per disjunct. The goal cannot
  be a disjunction at this stage (substitute_complicated_goal precluded it).

  Implemented by rebuilding actions/axioms vectors.
*/
void split_disjunctions(Task &task) {
    // Actions.
    std::vector<Action> new_actions;
    for (auto &a : task.actions) {
        if (a.precondition &&
            a.precondition->kind() == Condition::Kind::DISJUNCTION) {
            for (const auto &part : a.precondition->parts()) {
                Action copy = a;
                copy.precondition = part;
                copy.uniquify_variables();
                new_actions.push_back(std::move(copy));
            }
        } else {
            new_actions.push_back(std::move(a));
        }
    }
    // Effect conditions can also be disjunctions: duplicate the effect
    // within an action.
    for (auto &a : new_actions) {
        std::vector<Effect> new_effects;
        for (auto &e : a.effects) {
            if (e.condition &&
                e.condition->kind() == Condition::Kind::DISJUNCTION) {
                for (const auto &part : e.condition->parts()) {
                    Effect copy = e;
                    copy.condition = part;
                    new_effects.push_back(std::move(copy));
                }
            } else {
                new_effects.push_back(std::move(e));
            }
        }
        a.effects = std::move(new_effects);
    }
    task.actions = std::move(new_actions);

    // Axioms.
    std::vector<Axiom> new_axioms;
    for (auto &x : task.axioms) {
        if (x.condition &&
            x.condition->kind() == Condition::Kind::DISJUNCTION) {
            for (const auto &part : x.condition->parts()) {
                Axiom copy = x;
                copy.condition = part;
                copy.uniquify_variables();
                new_axioms.push_back(std::move(copy));
            }
        } else {
            new_axioms.push_back(std::move(x));
        }
    }
    task.axioms = std::move(new_axioms);
}

/* [5] move_existential_quantifiers ----------------------------------- */

ConditionPtr move_existential_recurse(const ConditionPtr &condition) {
    std::vector<ConditionPtr> existential_parts;
    std::vector<ConditionPtr> other_parts;
    for (const auto &part : condition->parts()) {
        auto p = move_existential_recurse(part);
        if (p->kind() == Condition::Kind::EXISTENTIAL)
            existential_parts.push_back(std::move(p));
        else
            other_parts.push_back(std::move(p));
    }
    if (existential_parts.empty()) return condition;

    if (condition->kind() == Condition::Kind::EXISTENTIAL) {
        const auto &q = static_cast<const ExistentialCondition &>(*condition);
        const auto &inner =
            static_cast<const ExistentialCondition &>(*existential_parts[0]);
        std::vector<TypedObject> new_params = q.parameters;
        for (const auto &p : inner.parameters) new_params.push_back(p);
        return std::make_shared<ExistentialCondition>(std::move(new_params),
                                                      inner.body);
    }
    // Conjunction: pull existentials out.
    std::vector<TypedObject> new_params;
    std::vector<ConditionPtr> new_conjunction_parts = other_parts;
    for (const auto &ep : existential_parts) {
        const auto &q = static_cast<const ExistentialCondition &>(*ep);
        for (const auto &p : q.parameters) new_params.push_back(p);
        for (const auto &b : q.body) new_conjunction_parts.push_back(b);
    }
    auto new_conjunction =
        std::make_shared<Conjunction>(std::move(new_conjunction_parts));
    return std::make_shared<ExistentialCondition>(
        std::move(new_params),
        std::vector<ConditionPtr>{new_conjunction});
}

void move_existential_quantifiers(Task &task) {
    for_each_condition(task,
        [&](auto /*get_tm*/, auto get_c, auto set_c) {
            auto c = get_c();
            if (c && c->has_existential_part())
                set_c(move_existential_recurse(c)->simplified());
        });
}

/* [5a-c] eliminate existential quantifiers --------------------------- */

void eliminate_existential_quantifiers_from_axioms(Task &task) {
    for (auto &x : task.axioms) {
        if (x.condition &&
            x.condition->kind() == Condition::Kind::EXISTENTIAL) {
            const auto &q =
                static_cast<const ExistentialCondition &>(*x.condition);
            for (const auto &p : q.parameters) x.parameters.push_back(p);
            x.condition = q.body[0];
        }
    }
}

void eliminate_existential_quantifiers_from_preconditions(Task &task) {
    for (auto &a : task.actions) {
        if (a.precondition &&
            a.precondition->kind() == Condition::Kind::EXISTENTIAL) {
            const auto &q =
                static_cast<const ExistentialCondition &>(*a.precondition);
            for (const auto &p : q.parameters) a.parameters.push_back(p);
            a.precondition = q.body[0];
        }
    }
}

void eliminate_existential_quantifiers_from_conditional_effects(Task &task) {
    for (auto &a : task.actions) {
        for (auto &e : a.effects) {
            if (e.condition &&
                e.condition->kind() == Condition::Kind::EXISTENTIAL) {
                const auto &q =
                    static_cast<const ExistentialCondition &>(*e.condition);
                for (const auto &p : q.parameters) e.parameters.push_back(p);
                e.condition = q.body[0];
            }
        }
    }
}

/* [7] verify_axiom_predicates ---------------------------------------- */

void verify_axiom_predicates(const Task &task) {
    std::set<std::string> axiom_names;
    for (const auto &x : task.axioms) axiom_names.insert(x.name);
    for (const auto &i : task.init) {
        if (std::holds_alternative<std::shared_ptr<const Atom>>(i)) {
            const auto &atom = std::get<std::shared_ptr<const Atom>>(i);
            if (atom && axiom_names.contains(atom->predicate)) {
                throw std::runtime_error(
                    "error: derived predicate '" + atom->predicate +
                    "' appears in :init fact");
            }
        }
    }
    for (const auto &a : task.actions) {
        for (const auto &e : a.effects) {
            if (!e.literal) continue;
            const auto &lit = static_cast<const Literal &>(*e.literal);
            if (axiom_names.contains(lit.predicate)) {
                throw std::runtime_error(
                    "error: derived predicate '" + lit.predicate +
                    "' appears in effect of action '" + a.name + "'");
            }
        }
    }
}
}

void normalize(Task &task) {
    remove_universal_quantifiers(task);
    substitute_complicated_goal(task);
    build_DNF(task);
    split_disjunctions(task);
    move_existential_quantifiers(task);
    eliminate_existential_quantifiers_from_axioms(task);
    eliminate_existential_quantifiers_from_preconditions(task);
    eliminate_existential_quantifiers_from_conditional_effects(task);
    verify_axiom_predicates(task);
}
}
