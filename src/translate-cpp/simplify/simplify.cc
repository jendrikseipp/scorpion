#include "simplify.h"

#include "../translate_options.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace translate::simplify {
using namespace sas;

namespace {
constexpr int ALWAYS_FALSE = -2;
constexpr int ALWAYS_TRUE  = -3;

struct DTG {
    int init;
    int size;
    std::vector<std::set<int>> arcs;
    explicit DTG(int init_val, int sz)
        : init(init_val), size(sz), arcs(sz) {}
    void add_arc(int u, int v) {
        if (u >= 0 && u < size && v >= 0 && v < size && u != v)
            arcs[u].insert(v);
    }
    std::set<int> reachable() const {
        std::set<int> seen;
        seen.insert(init);
        std::vector<int> stack = {init};
        while (!stack.empty()) {
            int n = stack.back(); stack.pop_back();
            if (n < 0 || n >= size) continue;
            for (int m : arcs[n])
                if (seen.insert(m).second) stack.push_back(m);
        }
        return seen;
    }
};

std::vector<DTG> build_dtgs(const SASTask &task) {
    std::vector<DTG> dtgs;
    dtgs.reserve(task.variables.ranges.size());
    for (std::size_t i = 0; i < task.variables.ranges.size(); ++i)
        dtgs.emplace_back(task.init.values[i], task.variables.ranges[i]);
    auto add_arc_var = [&](int var_no, int pre_spec, int post) {
        if (pre_spec == -1) {
            for (int p = 0; p < task.variables.ranges[var_no]; ++p)
                if (p != post) dtgs[var_no].add_arc(p, post);
        } else {
            dtgs[var_no].add_arc(pre_spec, post);
        }
    };
    auto effective_pre = [](int var_no,
                            const std::unordered_map<int, int> &conds,
                            const std::vector<VarVal> &eff_cond)
        -> std::optional<int> {
        auto it = conds.find(var_no);
        int result = (it == conds.end()) ? -1 : it->second;
        for (const auto &[cv, cval] : eff_cond) {
            if (cv == var_no) {
                if (result == -1) result = cval;
                else if (cval != result) return std::nullopt;
            }
        }
        return result;
    };
    for (const auto &op : task.operators) {
        std::unordered_map<int, int> conds;
        for (const auto &[v, val] : op.prevail) conds[v] = val;
        for (const auto &[v, pre, post, cond] : op.pre_post)
            if (pre != -1) conds[v] = pre;
        for (const auto &[v, pre, post, cond] : op.pre_post) {
            auto ep = effective_pre(v, conds, cond);
            if (ep) add_arc_var(v, *ep, post);
        }
    }
    for (const auto &ax : task.axioms)
        add_arc_var(ax.effect.first, -1, ax.effect.second);
    return dtgs;
}

struct Renaming {
    // -1 for removed variables; otherwise new variable index.
    std::vector<int> new_var_nos;
    // new_values[old_var][old_value]: ALWAYS_FALSE / ALWAYS_TRUE / new value
    std::vector<std::vector<int>> new_values;
    std::vector<int> new_sizes;
    int new_var_count = 0;
    int num_removed_values = 0;

    void register_variable(int old_size, int init_value,
                           const std::set<int> &new_domain) {
        if (new_domain.size() == 1) {
            std::vector<int> nv(old_size, ALWAYS_FALSE);
            nv[init_value] = ALWAYS_TRUE;
            new_var_nos.push_back(-1);
            new_values.push_back(std::move(nv));
            num_removed_values += old_size;
        } else {
            std::vector<int> nv(old_size, ALWAYS_FALSE);
            int counter = 0;
            for (int v = 0; v < old_size; ++v) {
                if (new_domain.contains(v)) nv[v] = counter++;
                else ++num_removed_values;
            }
            new_var_nos.push_back(new_var_count);
            new_values.push_back(std::move(nv));
            new_sizes.push_back(counter);
            ++new_var_count;
        }
    }

    std::pair<int, int> translate(int var, int value) const {
        return {new_var_nos[var], new_values[var][value]};
    }
};

bool convert_pairs(const Renaming &r, std::vector<VarVal> &pairs) {
    std::vector<VarVal> out;
    for (const auto &[v, val] : pairs) {
        auto [nv, nval] = r.translate(v, val);
        if (nval == ALWAYS_FALSE) return false;
        if (nval == ALWAYS_TRUE) continue;
        out.emplace_back(nv, nval);
    }
    pairs = std::move(out);
    return true;
}

void apply_to_variables(const Renaming &r, SASVariables &vars) {
    std::vector<int> new_axiom_layers(r.new_var_count);
    for (std::size_t old = 0; old < r.new_var_nos.size(); ++old) {
        int nv = r.new_var_nos[old];
        if (nv >= 0) new_axiom_layers[nv] = vars.axiom_layers[old];
    }
    vars.ranges = r.new_sizes;
    vars.axiom_layers = std::move(new_axiom_layers);
    std::vector<std::vector<std::string>> new_names(r.new_var_count);
    for (int nv = 0; nv < r.new_var_count; ++nv)
        new_names[nv].assign(r.new_sizes[nv], std::string());
    for (std::size_t var = 0; var < vars.value_names.size(); ++var) {
        int new_var = r.new_var_nos[var];
        if (new_var < 0) continue;
        for (std::size_t val = 0; val < vars.value_names[var].size(); ++val) {
            int new_val = r.new_values[var][val];
            if (new_val < 0) continue;
            new_names[new_var][new_val] = vars.value_names[var][val];
        }
    }
    vars.value_names = std::move(new_names);
}

void apply_to_mutexes(const Renaming &r, std::vector<SASMutexGroup> &mutexes) {
    std::vector<SASMutexGroup> out;
    for (auto &m : mutexes) {
        std::vector<VarVal> new_facts;
        for (const auto &[v, val] : m.facts) {
            auto [nv, nval] = r.translate(v, val);
            if (nval == ALWAYS_FALSE || nval == ALWAYS_TRUE) continue;
            new_facts.emplace_back(nv, nval);
        }
        if (new_facts.size() >= 2) {
            m.facts = std::move(new_facts);
            out.push_back(std::move(m));
        }
    }
    mutexes = std::move(out);
}

void apply_to_init(const Renaming &r, SASInit &init) {
    std::vector<int> new_values(r.new_var_count);
    for (std::size_t v = 0; v < init.values.size(); ++v) {
        int nv = r.new_var_nos[v];
        int new_val = r.new_values[v][init.values[v]];
        if (nv >= 0) new_values[nv] = new_val;
    }
    init.values = std::move(new_values);
}

void apply_to_goal(const Renaming &r, SASGoal &goal) {
    auto pairs = goal.pairs;
    if (!convert_pairs(r, pairs))
        throw Impossible();
    if (pairs.empty()) throw TriviallySolvable();
    goal.pairs = std::move(pairs);
}

std::optional<SASOperator> translate_operator(const Renaming &r,
                                              const SASOperator &op) {
    // Build applicability conditions (prevail + pre). Sorted by var; each
    // var appears at most once (preconditions can't conflict with
    // prevails, and SASOperator::validate guarantees pre uniqueness per
    // var). We use the sorted vector directly as the lookup table:
    // binary search is fast for ~5 entries and avoids the per-operator
    // unordered_map/unordered_set allocations that dominated this loop
    // on operator-heavy tasks (115 k operators on logistics/p01).
    std::vector<VarVal> applicability = op.prevail;
    for (const auto &[v, pre, post, cond] : op.pre_post) {
        if (pre != -1) applicability.emplace_back(v, pre);
    }
    std::ranges::sort(applicability);
    if (!convert_pairs(r, applicability))
        return std::nullopt;

    auto find_app = [&](int var) -> int {
        auto it = std::lower_bound(
            applicability.begin(), applicability.end(), var,
            [](const VarVal &p, int v) { return p.first < v; });
        if (it != applicability.end() && it->first == var) return it->second;
        return -1;
    };

    std::vector<int> pp_vars;
    pp_vars.reserve(op.pre_post.size());
    std::vector<std::tuple<int, int, int, std::vector<VarVal>>> new_pre_post;
    for (const auto &[var_no, pre, post, cond] : op.pre_post) {
        auto [new_var_no, new_post] = r.translate(var_no, post);
        if (new_post == ALWAYS_TRUE) continue;
        int new_pre = -1;
        if (pre != -1) {
            auto [_, np] = r.translate(var_no, pre);
            if (np == ALWAYS_FALSE) {
                // Shouldn't happen if applicability was converted ok.
                return std::nullopt;
            }
            new_pre = np;
        }
        if (new_post == new_pre) continue;
        std::vector<VarVal> new_cond = cond;
        if (!convert_pairs(r, new_cond)) continue;
        bool incompat = false;
        for (const auto &[cv, cval] : new_cond) {
            int prev = find_app(cv);
            if (prev != -1 && prev != cval) { incompat = true; break; }
        }
        if (incompat) continue;
        new_pre_post.emplace_back(new_var_no, new_pre, new_post,
                                  std::move(new_cond));
        pp_vars.push_back(new_var_no);
    }
    if (new_pre_post.empty() && !get_options().keep_no_ops)
        return std::nullopt;

    std::ranges::sort(pp_vars);
    pp_vars.erase(std::unique(pp_vars.begin(), pp_vars.end()), pp_vars.end());

    std::vector<VarVal> new_prevail;
    new_prevail.reserve(applicability.size());
    for (const auto &[v, val] : applicability) {
        if (!std::binary_search(pp_vars.begin(), pp_vars.end(), v))
            new_prevail.emplace_back(v, val);
    }
    // Canonical sort+uniq so the post-simplify operator stays in
    // Python-canonical pre_post order all the way to output.
    std::ranges::sort(new_pre_post);
    new_pre_post.erase(std::unique(new_pre_post.begin(), new_pre_post.end()),
                       new_pre_post.end());
    SASOperator out;
    out.name = op.name;
    out.prevail = std::move(new_prevail);
    out.pre_post = std::move(new_pre_post);
    out.cost = op.cost;
    return out;
}
}

void filter_unreachable_propositions(SASTask &task) {
    auto dtgs = build_dtgs(task);
    Renaming r;
    for (const auto &d : dtgs)
        r.register_variable(d.size, d.init, d.reachable());
    // Apply.
    apply_to_variables(r, task.variables);
    apply_to_mutexes(r, task.mutexes);
    apply_to_init(r, task.init);
    apply_to_goal(r, task.goal);
    std::vector<SASOperator> new_ops;
    int removed = 0;
    for (auto &op : task.operators) {
        auto nop = translate_operator(r, op);
        if (nop) new_ops.push_back(std::move(*nop));
        else ++removed;
    }
    std::cout << removed << " operators removed" << std::endl;
    task.operators = std::move(new_ops);
    std::vector<SASAxiom> new_ax;
    int ax_removed = 0;
    for (auto &ax : task.axioms) {
        std::vector<VarVal> cond = ax.condition;
        if (!convert_pairs(r, cond)) { ++ax_removed; continue; }
        auto [nv, nval] = r.translate(ax.effect.first, ax.effect.second);
        if (nval == ALWAYS_FALSE || nval == ALWAYS_TRUE) {
            ++ax_removed; continue;
        }
        ax.condition = std::move(cond);
        ax.effect = {nv, nval};
        new_ax.push_back(std::move(ax));
    }
    std::cout << ax_removed << " axioms removed" << std::endl;
    task.axioms = std::move(new_ax);
    std::cout << r.num_removed_values << " propositions removed" << std::endl;
}
}
