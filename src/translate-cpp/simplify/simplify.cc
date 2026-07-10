#include "simplify.h"

#include "../translate_options.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
namespace translate::simplify {
using namespace sas;

namespace {
constexpr int ALWAYS_FALSE = -2;
constexpr int ALWAYS_TRUE = -3;

struct DTG {
    int init;
    int size;
    // Specific transitions pre -> post (from operators with a precondition on
    // this variable). Flat vectors, not set<int>: the BFS skips already-seen
    // targets, so duplicate arcs are harmless and we avoid a tree node (plus
    // its malloc) per arc -- the hot spot of this phase on variables with large
    // domains.
    vector<vector<int>> arcs;
    // A "pre == -1" effect can set the variable to `post` from *any* value, so
    // `post` is reachable from init unconditionally. Recording that as one flag
    // avoids materializing an arc from every one of the (up to `size`) values
    // to `post` -- the quadratic blowup that dominated operator-heavy tasks
    // with wide variable domains (e.g. ferry).
    vector<char> unconditional;
    explicit DTG(int init_val, int sz)
        : init(init_val), size(sz), arcs(sz), unconditional(sz, 0) {
    }
    void add_arc(int u, int v) {
        if (u >= 0 && u < size && v >= 0 && v < size && u != v)
            arcs[u].push_back(v);
    }
    void add_unconditional(int v) {
        if (v >= 0 && v < size)
            unconditional[v] = 1;
    }
    // Dense membership vector (indexed by value, 1 if reachable from init):
    // values are 0..size-1, so the BFS marks and tests in O(1), and the
    // consumer only needs membership. Seeds are init plus every
    // unconditionally-reachable value.
    vector<char> reachable() const {
        vector<char> seen(size, 0);
        vector<int> stack;
        if (init >= 0 && init < size) {
            seen[init] = 1;
            stack.push_back(init);
        }
        for (int v = 0; v < size; ++v)
            if (unconditional[v] && !seen[v]) {
                seen[v] = 1;
                stack.push_back(v);
            }
        while (!stack.empty()) {
            int n = stack.back();
            stack.pop_back();
            for (int m : arcs[n])
                if (!seen[m]) {
                    seen[m] = 1;
                    stack.push_back(m);
                }
        }
        return seen;
    }
};

vector<DTG> build_dtgs(const SASTask &task) {
    vector<DTG> dtgs;
    dtgs.reserve(task.variables.ranges.size());
    for (size_t i = 0; i < task.variables.ranges.size(); ++i)
        dtgs.emplace_back(task.init.values[i], task.variables.ranges[i]);
    auto add_arc_var = [&](int var_no, int pre_spec, int post) {
        if (pre_spec == -1)
            dtgs[var_no].add_unconditional(post);
        else
            dtgs[var_no].add_arc(pre_spec, post);
    };
    auto effective_pre = [](int var_no, const vector<VarVal> &conds,
                            const vector<VarVal> &eff_cond) -> optional<int> {
        int result = -1;
        for (const auto &[cv, cval] : conds)
            if (cv == var_no) {
                result = cval;
                break;
            }
        for (const auto &[cv, cval] : eff_cond) {
            if (cv == var_no) {
                if (result == -1)
                    result = cval;
                else if (cval != result)
                    return nullopt;
            }
        }
        return result;
    };
    // Reused across operators (prevail and pre_post touch disjoint variables,
    // so no key collides): clear() keeps the buffer, so the per-operator
    // condition table costs no allocation after warmup, unlike the previous
    // unordered_map built fresh for each of millions of operators.
    vector<VarVal> conds;
    for (const auto &op : task.operators) {
        conds.clear();
        for (const auto &[v, val] : op.prevail)
            conds.emplace_back(v, val);
        for (const auto &[v, pre, post, cond] : op.pre_post)
            if (pre != -1)
                conds.emplace_back(v, pre);
        for (const auto &[v, pre, post, cond] : op.pre_post) {
            auto ep = effective_pre(v, conds, cond);
            if (ep)
                add_arc_var(v, *ep, post);
        }
    }
    for (const auto &ax : task.axioms)
        add_arc_var(ax.effect.first, -1, ax.effect.second);
    return dtgs;
}

struct Renaming {
    // -1 for removed variables; otherwise new variable index.
    vector<int> new_var_nos;
    // new_values[old_var][old_value]: ALWAYS_FALSE / ALWAYS_TRUE / new value
    vector<vector<int>> new_values;
    vector<int> new_sizes;
    int new_var_count = 0;
    int num_removed_values = 0;

    void register_variable(
        int old_size, int init_value, const vector<char> &new_domain) {
        int domain_size = 0;
        for (int v = 0; v < old_size; ++v)
            domain_size += new_domain[v];
        if (domain_size == 1) {
            vector<int> nv(old_size, ALWAYS_FALSE);
            nv[init_value] = ALWAYS_TRUE;
            new_var_nos.push_back(-1);
            new_values.push_back(move(nv));
            num_removed_values += old_size;
        } else {
            vector<int> nv(old_size, ALWAYS_FALSE);
            int counter = 0;
            for (int v = 0; v < old_size; ++v) {
                if (new_domain[v])
                    nv[v] = counter++;
                else
                    ++num_removed_values;
            }
            new_var_nos.push_back(new_var_count);
            new_values.push_back(move(nv));
            new_sizes.push_back(counter);
            ++new_var_count;
        }
    }

    pair<int, int> translate(int var, int value) const {
        return {new_var_nos[var], new_values[var][value]};
    }
};

bool convert_pairs(const Renaming &r, vector<VarVal> &pairs) {
    vector<VarVal> out;
    for (const auto &[v, val] : pairs) {
        auto [nv, nval] = r.translate(v, val);
        if (nval == ALWAYS_FALSE)
            return false;
        if (nval == ALWAYS_TRUE)
            continue;
        out.emplace_back(nv, nval);
    }
    pairs = move(out);
    return true;
}

void apply_to_variables(const Renaming &r, SASVariables &vars) {
    vector<int> new_axiom_layers(r.new_var_count);
    for (size_t old = 0; old < r.new_var_nos.size(); ++old) {
        int nv = r.new_var_nos[old];
        if (nv >= 0)
            new_axiom_layers[nv] = vars.axiom_layers[old];
    }
    vars.ranges = r.new_sizes;
    vars.axiom_layers = move(new_axiom_layers);
    vector<vector<string>> new_names(r.new_var_count);
    for (int nv = 0; nv < r.new_var_count; ++nv)
        new_names[nv].assign(r.new_sizes[nv], string());
    for (size_t var = 0; var < vars.value_names.size(); ++var) {
        int new_var = r.new_var_nos[var];
        if (new_var < 0)
            continue;
        for (size_t val = 0; val < vars.value_names[var].size(); ++val) {
            int new_val = r.new_values[var][val];
            if (new_val < 0)
                continue;
            new_names[new_var][new_val] = vars.value_names[var][val];
        }
    }
    vars.value_names = move(new_names);
}

void apply_to_mutexes(const Renaming &r, vector<SASMutexGroup> &mutexes) {
    vector<SASMutexGroup> out;
    for (auto &m : mutexes) {
        vector<VarVal> new_facts;
        for (const auto &[v, val] : m.facts) {
            auto [nv, nval] = r.translate(v, val);
            if (nval == ALWAYS_FALSE || nval == ALWAYS_TRUE)
                continue;
            new_facts.emplace_back(nv, nval);
        }
        if (new_facts.size() >= 2) {
            m.facts = move(new_facts);
            out.push_back(move(m));
        }
    }
    mutexes = move(out);
}

void apply_to_init(const Renaming &r, SASInit &init) {
    vector<int> new_values(r.new_var_count);
    for (size_t v = 0; v < init.values.size(); ++v) {
        int nv = r.new_var_nos[v];
        int new_val = r.new_values[v][init.values[v]];
        if (nv >= 0)
            new_values[nv] = new_val;
    }
    init.values = move(new_values);
}

void apply_to_goal(const Renaming &r, SASGoal &goal) {
    auto pairs = goal.pairs;
    if (!convert_pairs(r, pairs))
        throw Impossible();
    if (pairs.empty())
        throw TriviallySolvable();
    goal.pairs = move(pairs);
}

// Renumber `op`'s variables under `r` in place, returning true to keep it or
// false to drop it. Modifying in place (rather than returning a fresh operator)
// avoids a second full operators vector -- the double representation was the
// peak on operator-heavy tasks -- and keeps the (unchanged) name and cost
// without copying them. All of `op` is read before its fields are reassigned.
bool translate_operator(const Renaming &r, SASOperator &op) {
    // Build applicability conditions (prevail + pre). Sorted by var; each var
    // appears at most once (a prevail and a pre never name the same var, and
    // pre_post is canonicalized to one entry per var). We use the sorted vector
    // directly as the lookup table: binary search is fast for ~5 entries and
    // avoids the per-operator unordered_map/unordered_set allocations that
    // dominated this loop
    // on operator-heavy tasks (115 k operators on logistics/p01).
    vector<VarVal> applicability = op.prevail;
    for (const auto &[v, pre, post, cond] : op.pre_post) {
        if (pre != -1)
            applicability.emplace_back(v, pre);
    }
    ranges::sort(applicability);
    if (!convert_pairs(r, applicability))
        return false;

    auto find_app = [&](int var) -> int {
        auto it = lower_bound(
            applicability.begin(), applicability.end(), var,
            [](const VarVal &p, int v) { return p.first < v; });
        if (it != applicability.end() && it->first == var)
            return it->second;
        return -1;
    };

    vector<int> pp_vars;
    pp_vars.reserve(op.pre_post.size());
    vector<PrePost> new_pre_post;
    for (const auto &[var_no, pre, post, cond] : op.pre_post) {
        auto [new_var_no, new_post] = r.translate(var_no, post);
        if (new_post == ALWAYS_TRUE)
            continue;
        int new_pre = -1;
        if (pre != -1) {
            auto [_, np] = r.translate(var_no, pre);
            if (np == ALWAYS_FALSE) {
                // Shouldn't happen if applicability was converted ok.
                return false;
            }
            new_pre = np;
        }
        if (new_post == new_pre)
            continue;
        vector<VarVal> new_cond = cond;
        if (!convert_pairs(r, new_cond))
            continue;
        bool incompat = false;
        for (const auto &[cv, cval] : new_cond) {
            int prev = find_app(cv);
            if (prev != -1 && prev != cval) {
                incompat = true;
                break;
            }
        }
        if (incompat)
            continue;
        new_pre_post.emplace_back(
            new_var_no, new_pre, new_post, move(new_cond));
        pp_vars.push_back(new_var_no);
    }
    if (new_pre_post.empty() && !get_options().keep_no_ops)
        return false;

    ranges::sort(pp_vars);
    pp_vars.erase(unique(pp_vars.begin(), pp_vars.end()), pp_vars.end());

    vector<VarVal> new_prevail;
    new_prevail.reserve(applicability.size());
    for (const auto &[v, val] : applicability) {
        if (!binary_search(pp_vars.begin(), pp_vars.end(), v))
            new_prevail.emplace_back(v, val);
    }
    // Canonical sort+uniq so the post-simplify operator stays in
    // Python-canonical pre_post order all the way to output.
    ranges::sort(new_pre_post);
    new_pre_post.erase(
        unique(new_pre_post.begin(), new_pre_post.end()), new_pre_post.end());
    op.prevail = move(new_prevail);
    op.pre_post = move(new_pre_post);
    // name and cost are unchanged -- left in place, not copied.
    return true;
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
    // Renumber operators in place and compact out the removed ones, so a
    // second full operators vector never coexists with the first (that double
    // representation was the peak on operator-heavy tasks). Order is preserved.
    size_t removed = 0;
    size_t kept = 0;
    for (size_t i = 0; i < task.operators.size(); ++i) {
        if (translate_operator(r, task.operators[i])) {
            if (kept != i)
                task.operators[kept] = move(task.operators[i]);
            ++kept;
        } else {
            ++removed;
        }
    }
    task.operators.resize(kept);
    cout << removed << " operators removed" << endl;
    vector<SASAxiom> new_ax;
    int ax_removed = 0;
    for (auto &ax : task.axioms) {
        vector<VarVal> cond = ax.condition;
        if (!convert_pairs(r, cond)) {
            ++ax_removed;
            continue;
        }
        auto [nv, nval] = r.translate(ax.effect.first, ax.effect.second);
        if (nval == ALWAYS_FALSE || nval == ALWAYS_TRUE) {
            ++ax_removed;
            continue;
        }
        ax.condition = move(cond);
        ax.effect = {nv, nval};
        new_ax.push_back(move(ax));
    }
    cout << ax_removed << " axioms removed" << endl;
    task.axioms = move(new_ax);
    cout << r.num_removed_values << " propositions removed" << endl;
}
}
