#include "split.h"

#include "../utils/graph.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace translate::grounding {
namespace {
/* Variables sharing across atoms induce connected components. */
std::vector<std::vector<Atom>> get_connected_conditions(
    const std::vector<Atom> &conditions) {
    if (conditions.empty()) return {};
    // Build var -> list-of-condition-indices.
    std::unordered_map<std::string, std::vector<int>> var_to_conds;
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        for (const auto &arg : conditions[i].args) {
            if (arg.is_symbol()) {
                const std::string &s = arg.name();
                if (!s.empty() && s.front() == '?')
                    var_to_conds[s].push_back(static_cast<int>(i));
            }
        }
    }
    std::vector<std::pair<int, int>> edges;
    for (const auto &[v, idxs] : var_to_conds) {
        for (std::size_t k = 1; k < idxs.size(); ++k)
            edges.emplace_back(idxs[0], idxs[k]);
    }
    auto comp = utils::connected_components(conditions.size(), edges);
    int n_comp = 0;
    for (int c : comp) n_comp = std::max(n_comp, c + 1);
    std::vector<std::vector<Atom>> result(n_comp);
    for (std::size_t i = 0; i < conditions.size(); ++i)
        result[comp[i]].push_back(conditions[i]);
    // Sort each component by atom for deterministic output.
    for (auto &c : result)
        std::sort(c.begin(), c.end());
    std::ranges::sort(result);
    return result;
}

Rule project_rule(const Atom &target_effect,
                  const std::vector<Atom> &conditions, Program &prog) {
    auto cond_vars = get_variables(conditions);
    auto eff_vars = get_variables(target_effect);
    std::vector<std::string> retained;
    for (const auto &v : eff_vars)
        if (cond_vars.contains(v)) retained.push_back(v);
    std::ranges::sort(retained);
    std::vector<Arg> args;
    args.reserve(retained.size());
    for (const auto &v : retained) args.emplace_back(v);
    Atom effect(prog.new_predicate_name(), std::move(args));
    return Rule{conditions, effect};
}

/* ----- Greedy binary join ----- */

class OccurrencesTracker {
public:
    std::unordered_map<std::string, int> occ;
    void update(const Atom &a, int delta) {
        for (const auto &arg : a.args) {
            if (arg.is_symbol()) {
                const std::string &s = arg.name();
                if (!s.empty() && s.front() == '?') {
                    occ[s] += delta;
                    if (occ[s] == 0) occ.erase(s);
                }
            }
        }
    }
    std::unordered_set<std::string> variables() const {
        std::unordered_set<std::string> out;
        for (const auto &[v, _] : occ) out.insert(v);
        return out;
    }
};

using Cost = std::tuple<int, int, int>;

Cost compute_join_cost(const Atom &left, const Atom &right) {
    auto lv = get_variables(left);
    auto rv = get_variables(right);
    if (lv.size() > rv.size()) std::swap(lv, rv);
    int common = 0;
    for (const auto &v : lv) if (rv.contains(v)) ++common;
    return {static_cast<int>(lv.size()) - common,
            static_cast<int>(rv.size()) - common,
            -common};
}

std::vector<Rule> greedy_join(const Rule &rule, Program &prog) {
    std::vector<Atom> joinees = rule.conditions;
    OccurrencesTracker occ;
    occ.update(rule.effect, +1);
    for (const auto &c : rule.conditions) occ.update(c, +1);

    std::vector<Rule> result;
    while (joinees.size() >= 2) {
        // Find min-cost pair.
        Cost best{INT_MAX, INT_MAX, INT_MAX};
        std::size_t bi = 0, bj = 0;
        for (std::size_t i = 0; i < joinees.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                Cost c = compute_join_cost(joinees[i], joinees[j]);
                if (c < best) { best = c; bi = i; bj = j; }
            }
        }
        Atom left = joinees[bi];
        Atom right = joinees[bj];
        // Remove larger index first.
        joinees.erase(joinees.begin() + bi);
        joinees.erase(joinees.begin() + bj);
        occ.update(left, -1);
        occ.update(right, -1);

        auto lv = get_variables(left);
        auto rv = get_variables(right);
        std::unordered_set<std::string> common_vars;
        for (const auto &v : lv) if (rv.contains(v)) common_vars.insert(v);
        std::unordered_set<std::string> condition_vars = lv;
        for (const auto &v : rv) condition_vars.insert(v);
        auto live = occ.variables();
        std::unordered_set<std::string> effect_vars;
        for (const auto &v : live)
            if (condition_vars.contains(v)) effect_vars.insert(v);

        auto maybe_project = [&](const Atom &joinee) -> Atom {
            auto jv = get_variables(joinee);
            std::unordered_set<std::string> retained;
            for (const auto &v : jv)
                if (effect_vars.contains(v) || common_vars.contains(v))
                    retained.insert(v);
            if (retained == jv) return joinee;
            std::vector<std::string> sorted_ret(retained.begin(),
                                                retained.end());
            std::ranges::sort(sorted_ret);
            std::vector<Arg> args;
            args.reserve(sorted_ret.size());
            for (const auto &v : sorted_ret) args.emplace_back(v);
            Atom effect(prog.new_predicate_name(), std::move(args));
            Rule pr{{joinee}, effect, RuleKind::PROJECT};
            result.push_back(pr);
            return effect;
        };
        Atom new_left = maybe_project(left);
        Atom new_right = maybe_project(right);

        std::vector<std::string> sorted_eff(effect_vars.begin(),
                                            effect_vars.end());
        std::ranges::sort(sorted_eff);
        std::vector<Arg> join_args;
        join_args.reserve(sorted_eff.size());
        for (const auto &v : sorted_eff) join_args.emplace_back(v);
        Atom join_effect(prog.new_predicate_name(), std::move(join_args));
        Rule join_rule{{new_left, new_right}, join_effect, RuleKind::JOIN};
        result.push_back(join_rule);
        joinees.push_back(join_effect);
        occ.update(join_effect, +1);
    }
    // Final result rule: replace last result's effect with the rule's
    // original effect.
    if (!result.empty()) {
        result.back().effect = rule.effect;
    } else {
        // 0 or 1 condition: last result is empty; nothing to do.
    }
    return result;
}

std::vector<Rule> split_into_binary_rules(Rule rule, Program &prog) {
    if (rule.conditions.size() <= 1) {
        rule.kind = RuleKind::PROJECT;
        return {rule};
    }
    return greedy_join(rule, prog);
}

std::vector<Rule> split_rule(const Rule &rule, Program &prog) {
    std::vector<Atom> important, trivial;
    for (const auto &c : rule.conditions) {
        bool has_var = false;
        for (const auto &a : c.args) {
            if (a.is_symbol()) {
                const std::string &s = a.name();
                if (!s.empty() && s.front() == '?') { has_var = true; break; }
            }
        }
        (has_var ? important : trivial).push_back(c);
    }
    auto components = get_connected_conditions(important);
    if (components.size() == 1 && trivial.empty()) {
        return split_into_binary_rules(rule, prog);
    }
    std::vector<Rule> projected_rules;
    for (auto &comp : components)
        projected_rules.push_back(project_rule(rule.effect, comp, prog));
    std::vector<Rule> result;
    for (auto &pr : projected_rules) {
        auto sub = split_into_binary_rules(pr, prog);
        for (auto &r : sub) result.push_back(std::move(r));
    }
    std::vector<Atom> combining_conds;
    for (auto &pr : projected_rules) combining_conds.push_back(pr.effect);
    for (auto &t : trivial) combining_conds.push_back(t);
    Rule combining{combining_conds, rule.effect};
    combining.kind = (combining_conds.size() >= 2) ? RuleKind::PRODUCT
                                                   : RuleKind::PROJECT;
    result.push_back(combining);
    return result;
}
}

void split_rules(Program &prog) {
    std::vector<Rule> new_rules;
    for (const auto &r : prog.rules) {
        auto sub = split_rule(r, prog);
        for (auto &nr : sub) new_rules.push_back(std::move(nr));
    }
    prog.rules = std::move(new_rules);
}
}
