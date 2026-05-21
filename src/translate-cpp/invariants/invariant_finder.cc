#include "invariant_finder.h"

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"
#include "../pddl/effect.h"
#include "../pddl/predicate.h"
#include "../pddl/task.h"
#include "../translate_options.h"
#include "../utils/timer.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace translate::invariants {
using namespace pddl;

BalanceChecker::BalanceChecker(
    const Task &task,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters)
    : random_(314159) {
    patched_actions_.reserve(task.actions.size());
    heavy_actions_.reserve(task.actions.size());
    for (std::size_t i = 0; i < task.actions.size(); ++i) {
        const Action &act = task.actions[i];
        Action patched = act;
        // Add inequality preconditions based on reachable parameter tuples.
        if (reachable_action_parameters &&
            i < reachable_action_parameters->size() &&
            act.parameters.size() >= 2) {
            const auto &params = (*reachable_action_parameters)[i];
            std::vector<std::pair<int, int>> inequal_pairs;
            for (std::size_t p1 = 0; p1 < act.parameters.size(); ++p1) {
                for (std::size_t p2 = p1 + 1; p2 < act.parameters.size();
                     ++p2) {
                    bool ever_equal = false;
                    for (const auto &t : params)
                        if (t.size() > p2 && t[p1] == t[p2]) {
                            ever_equal = true; break;
                        }
                    if (!ever_equal) inequal_pairs.emplace_back(
                        static_cast<int>(p1), static_cast<int>(p2));
                }
            }
            if (!inequal_pairs.empty()) {
                std::vector<ConditionPtr> parts;
                parts.push_back(patched.precondition);
                for (const auto &[p1, p2] : inequal_pairs) {
                    parts.push_back(std::make_shared<NegatedAtom>(
                        "=", std::vector<std::string>{
                                 act.parameters[p1].name,
                                 act.parameters[p2].name}));
                }
                patched.precondition =
                    std::make_shared<Conjunction>(std::move(parts))
                        ->simplified();
            }
        }
        patched_actions_.push_back(std::move(patched));
    }
    for (auto &patched : patched_actions_) {
        // Build heavy action: duplicate universal effects.
        std::vector<Effect> heavy_effects;
        bool any_universal = false;
        for (const auto &eff : patched.effects) {
            heavy_effects.push_back(eff);
            if (!eff.parameters.empty()) {
                any_universal = true;
                heavy_effects.push_back(eff);
            }
        }
        Action heavy = patched;
        if (any_universal) heavy.effects = std::move(heavy_effects);
        heavy_actions_.push_back(std::move(heavy));
    }
    for (std::size_t i = 0; i < patched_actions_.size(); ++i) {
        action_to_heavy_[&patched_actions_[i]] = &heavy_actions_[i];
        for (const auto &eff : patched_actions_[i].effects) {
            if (!eff.literal) continue;
            const auto &lit = static_cast<const Literal &>(*eff.literal);
            if (lit.negated()) continue;
            auto &list = predicates_to_add_actions_[lit.predicate];
            if (list.empty() || list.back() != &patched_actions_[i])
                list.push_back(&patched_actions_[i]);
        }
    }
}

const std::vector<const Action *> &BalanceChecker::get_threats(
    const std::string &predicate) const {
    auto it = predicates_to_add_actions_.find(predicate);
    if (it == predicates_to_add_actions_.end()) return empty_;
    return it->second;
}

const Action *BalanceChecker::get_heavy_action(const Action *action) const {
    auto it = action_to_heavy_.find(action);
    return it == action_to_heavy_.end() ? nullptr : it->second;
}

int BalanceChecker::next_index(std::size_t upper_bound) {
    std::uniform_int_distribution<int> dist(
        0, static_cast<int>(upper_bound) - 1);
    return dist(random_);
}

namespace {
std::vector<const Predicate *> get_fluents(const Task &task) {
    std::unordered_set<std::string> fluent_names;
    for (const auto &a : task.actions)
        for (const auto &eff : a.effects) {
            if (!eff.literal) continue;
            const auto &lit = static_cast<const Literal &>(*eff.literal);
            fluent_names.insert(lit.predicate);
        }
    std::vector<const Predicate *> out;
    for (const auto &p : task.predicates)
        if (fluent_names.count(p.name)) out.push_back(&p);
    return out;
}

std::vector<Invariant> initial_invariants(const Task &task, int limit) {
    std::vector<Invariant> result;
    auto fluents = get_fluents(task);
    for (const auto *p : fluents) {
        if (static_cast<int>(result.size()) >= limit) break;
        std::vector<int> all_args(p->arguments.size());
        for (std::size_t i = 0; i < all_args.size(); ++i)
            all_args[i] = static_cast<int>(i);
        result.push_back(Invariant({InvariantPart(p->name, all_args, -1)}));
        for (std::size_t omitted = 0; omitted < p->arguments.size();
             ++omitted) {
            std::vector<int> inv_args;
            for (std::size_t i = 0; i < omitted; ++i)
                inv_args.push_back(static_cast<int>(i));
            inv_args.push_back(COUNTED);
            for (std::size_t i = omitted;
                 i + 1 < p->arguments.size(); ++i)
                inv_args.push_back(static_cast<int>(i));
            if (static_cast<int>(result.size()) >= limit) break;
            result.push_back(Invariant({InvariantPart(
                p->name, std::move(inv_args), static_cast<int>(omitted))}));
        }
    }
    return result;
}
}

std::vector<Invariant> find_invariants(
    const Task &task,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters) {
    const Options &opts = get_options();
    int limit = opts.invariant_generation_max_candidates;
    auto initial = initial_invariants(task, limit);
    std::cout << initial.size() << " initial candidates" << std::endl;
    std::deque<Invariant> queue(initial.begin(), initial.end());
    std::unordered_set<Invariant, InvariantHash> seen(initial.begin(),
                                                      initial.end());
    BalanceChecker checker(task, reachable_action_parameters);

    auto enqueue = [&](Invariant inv) {
        if (static_cast<int>(seen.size()) >= limit) return;
        if (seen.insert(inv).second) queue.push_back(std::move(inv));
    };

    utils::Timer t;
    std::vector<Invariant> confirmed;
    while (!queue.empty()) {
        Invariant cand = std::move(queue.front());
        queue.pop_front();
        if (t.seconds() > opts.invariant_generation_max_time) {
            std::cout << "Time limit reached, aborting invariant generation"
                      << std::endl;
            break;
        }
        if (cand.check_balance(checker, enqueue))
            confirmed.push_back(std::move(cand));
    }
    return confirmed;
}

std::vector<std::vector<ConditionPtr>> get_groups(
    const Task &task,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters) {
    std::cout << "Finding invariants..." << std::endl;
    auto invariants = find_invariants(task, reachable_action_parameters);
    std::cout << "Checking invariant weight..." << std::endl;

    // Group by predicate.
    std::unordered_map<std::string, std::vector<const Invariant *>>
        predicate_to_inv;
    for (const auto &inv : invariants) {
        for (const auto &part : inv.parts)
            predicate_to_inv[part.predicate].push_back(&inv);
    }
    // Iterate init facts and identify non-empty groups.
    using GroupKey = std::pair<const Invariant *, std::vector<std::string>>;
    struct GroupKeyHash {
        std::size_t operator()(const GroupKey &k) const noexcept {
            std::size_t h = std::hash<const Invariant *>{}(k.first);
            for (const auto &s : k.second)
                h ^= std::hash<std::string>{}(s) + 0x9e3779b9u +
                     (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<GroupKey, int, GroupKeyHash> nonempty_count;
    std::vector<GroupKey> order; // stable insertion order
    for (const auto &elem : task.init) {
        if (!std::holds_alternative<std::shared_ptr<const Atom>>(elem))
            continue;
        const auto &ap = std::get<std::shared_ptr<const Atom>>(elem);
        if (!ap) continue;
        auto it = predicate_to_inv.find(ap->predicate);
        if (it == predicate_to_inv.end()) continue;
        for (const auto *inv : it->second) {
            auto params = inv->get_parameters(*ap);
            GroupKey key{inv, params};
            auto [iter, inserted] = nonempty_count.emplace(key, 1);
            if (inserted) order.push_back(key);
            else iter->second++;
        }
    }
    std::vector<std::vector<ConditionPtr>> result;
    for (const auto &key : order) {
        if (nonempty_count[key] > 1) continue;
        std::vector<ConditionPtr> group;
        auto sorted_parts = key.first->parts;
        std::sort(sorted_parts.begin(), sorted_parts.end());
        for (const auto &part : sorted_parts)
            group.push_back(part.instantiate(key.second));
        result.push_back(std::move(group));
    }
    return result;
}
}
