#include "constraints.h"

#include <algorithm>
#include <functional>

using namespace std;
namespace translate::invariants {
size_t TermHash::operator()(const Term &t) const noexcept {
    if (auto *s = get_if<string>(&t))
        return hash<string>{}(*s);
    return hash<int>{}(get<int>(t)) ^ 0xabcdef01u;
}

bool term_less(const Term &a, const Term &b) {
    // Order: strings before ints; within strings, lexicographic; within
    // ints, numeric.
    if (a.index() != b.index())
        return a.index() < b.index();
    if (auto *as = get_if<string>(&a))
        return *as < get<string>(b);
    return get<int>(a) < get<int>(b);
}

bool is_variable_or_param(const Term &t) {
    if (auto *s = get_if<string>(&t))
        return !s->empty() && s->front() == '?';
    return true; // int (invariant parameter) counts as a variable-like term
}

bool is_object(const Term &t) {
    if (auto *s = get_if<string>(&t))
        return !s->empty() && s->front() != '?';
    return false;
}

string term_to_string(const Term &t) {
    if (auto *s = get_if<string>(&t)) return *s;
    return "@p" + to_string(get<int>(t));
}

namespace {
struct UnionFind {
    unordered_map<Term, Term, TermHash> parent;
    Term &find(const Term &x) {
        auto it = parent.find(x);
        if (it == parent.end()) {
            parent.emplace(x, x);
            return parent[x];
        }
        if (it->second == x)
            return it->second;
        Term root = find(it->second);
        parent[x] = root;
        return parent[x];
    }
    void unite(const Term &a, const Term &b) {
        auto ra = find(a), rb = find(b);
        if (!(ra == rb)) parent[ra] = rb;
    }
};
}

void EqualityConjunction::compute_representatives() {
    UnionFind uf;
    for (const auto &[a, b] : equalities) {
        uf.parent[a] = a;
        uf.parent[b] = b;
    }
    for (const auto &[a, b] : equalities) uf.unite(a, b);

    // Group by root.
    unordered_map<Term, vector<Term>, TermHash> classes;
    for (const auto &[t, _] : uf.parent) {
        Term r = uf.find(t);
        classes[r].push_back(t);
    }
    representative_.clear();
    for (const auto &[root, members] : classes) {
        // Find at most one object; prioritize objects over variables/ints.
        vector<Term> objects;
        vector<Term> non_objects;
        for (const auto &m : members) {
            if (is_object(m)) objects.push_back(m);
            else non_objects.push_back(m);
        }
        if (objects.size() >= 2) {
            consistent_ = false;
            representative_.clear();
            return;
        }
        Term rep = objects.empty() ? non_objects.front() : objects.front();
        for (const auto &m : members) representative_[m] = rep;
    }
    consistent_ = true;
}

bool EqualityConjunction::is_consistent() {
    if (!consistent_) compute_representatives();
    return *consistent_;
}

const unordered_map<Term, Term, TermHash> *
EqualityConjunction::get_representative() {
    if (!consistent_) compute_representatives();
    if (!*consistent_) return nullptr;
    return &representative_;
}

void ConstraintSystem::extend(const ConstraintSystem &other) {
    for (const auto &dnf : other.equality_DNFs)
        equality_DNFs.push_back(dnf);
    for (const auto &d : other.ineq_disjunctions)
        ineq_disjunctions.push_back(d);
    for (const auto &s : other.not_constant)
        not_constant.push_back(s);
}

namespace {
// Enumerate cartesian product of choices, calling fn(picks) for each.
template<class Fn>
void cartesian_product(
    const vector<vector<EqualityConjunction>> &dnfs,
    vector<const EqualityConjunction *> &picks, size_t depth, Fn fn) {
    if (depth == dnfs.size()) { fn(picks); return; }
    for (const auto &choice : dnfs[depth]) {
        picks.push_back(&choice);
        cartesian_product(dnfs, picks, depth + 1, fn);
        picks.pop_back();
    }
}
}

bool ConstraintSystem::is_solvable() const {
    bool found = false;
    vector<const EqualityConjunction *> picks;
    cartesian_product(equality_DNFs, picks, 0,
        [&](const vector<const EqualityConjunction *> &p) {
            if (found) return;
            // Combine equalities.
            vector<pair<Term, Term>> all;
            for (const auto *c : p)
                for (const auto &e : c->equalities) all.push_back(e);
            EqualityConjunction combined(move(all));
            if (!combined.is_consistent()) return;
            const auto *rep = combined.get_representative();
            // not_constant check.
            auto lookup = [&](const Term &t) -> Term {
                auto it = rep->find(t);
                return it == rep->end() ? t : it->second;
            };
            bool bad = false;
            for (const auto &nc : not_constant) {
                Term r = lookup(Term(nc));
                if (is_object(r)) { bad = true; break; }
            }
            if (bad) return;
            // Inequality disjunctions.
            for (const auto &d : ineq_disjunctions) {
                bool any_ok = false;
                for (const auto &[x, y] : d.parts) {
                    if (!(lookup(x) == lookup(y))) {
                        any_ok = true; break;
                    }
                }
                if (!any_ok) { bad = true; break; }
            }
            if (bad) return;
            found = true;
        });
    return found;
}
}
