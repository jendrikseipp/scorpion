#include "program.h"

#include <algorithm>
#include <iostream>

namespace translate::grounding {
SymbolTable &symbols() {
    static SymbolTable table;
    return table;
}

namespace {
inline void hash_combine(std::size_t &seed, std::size_t v) {
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}

bool Atom::operator<(const Atom &other) const {
    if (predicate != other.predicate) return predicate < other.predicate;
    if (args.size() != other.args.size()) return args.size() < other.args.size();
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Arg &a = args[i];
        const Arg &b = other.args[i];
        // Fast path: both symbols (the case for ground atoms) compares the
        // interned names by reference, avoiding the per-element heap
        // allocation that arg_to_string would incur in the sort comparator.
        if (a.is_symbol() && b.is_symbol()) {
            const std::string &an = a.name();
            const std::string &bn = b.name();
            if (an != bn) return an < bn;
        } else {
            const std::string as = arg_to_string(a);
            const std::string bs = arg_to_string(b);
            if (as != bs) return as < bs;
        }
    }
    return false;
}

std::size_t AtomHash::operator()(const Atom &a) const noexcept {
    std::size_t h = std::hash<std::string>{}(a.predicate);
    for (const auto &x : a.args)
        hash_combine(h, std::hash<int>{}(x.v));
    return h;
}

std::ostream &operator<<(std::ostream &os, const Atom &a) {
    os << a.predicate << "(";
    for (std::size_t i = 0; i < a.args.size(); ++i) {
        if (i) os << ", ";
        if (a.args[i].is_symbol()) os << a.args[i].name();
        else os << a.args[i].position();
    }
    os << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os, const Rule &r) {
    os << r.effect << " :- ";
    for (std::size_t i = 0; i < r.conditions.size(); ++i) {
        if (i) os << ", ";
        os << r.conditions[i];
    }
    return os;
}

void Program::add_fact(Atom atom) {
    for (const auto &a : atom.args) {
        if (a.is_symbol())
            objects.insert(a.name());
    }
    facts.push_back(std::move(atom));
}

void Program::add_rule(Rule rule) {
    rules.push_back(std::move(rule));
}

std::string Program::new_predicate_name() {
    return "p$" + std::to_string(next_aux_id_++);
}

std::unordered_set<std::string> get_variables(const Atom &atom) {
    std::unordered_set<std::string> out;
    for (const auto &a : atom.args) {
        if (a.is_symbol()) {
            const std::string &s = a.name();
            if (!s.empty() && s.front() == '?')
                out.insert(s);
        }
    }
    return out;
}

std::unordered_set<std::string> get_variables(const std::vector<Atom> &atoms) {
    std::unordered_set<std::string> out;
    for (const auto &a : atoms) {
        auto sub = get_variables(a);
        out.insert(sub.begin(), sub.end());
    }
    return out;
}

bool Rule::rename_duplicate_variables() {
    auto rename = [](Atom &atom, std::vector<Atom> &extra) {
        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < atom.args.size(); ++i) {
            if (!atom.args[i].is_symbol()) continue;
            // Copy: assigning a new (interned) name below may realloc the
            // symbol table and invalidate a reference into it.
            std::string s = atom.args[i].name();
            if (s.empty() || s.front() != '?') continue;
            if (seen.contains(s)) {
                std::string new_name = s + "@" + std::to_string(extra.size());
                atom.args[i] = new_name;
                extra.push_back(Atom("=", {Arg(s), Arg(new_name)}));
            } else {
                seen.insert(s);
            }
        }
    };
    std::vector<Atom> extra;
    rename(effect, extra);
    for (auto &cond : conditions) rename(cond, extra);
    for (auto &c : extra) conditions.push_back(std::move(c));
    return !extra.empty();
}

namespace {
bool has_unbound_effect_vars(const Rule &r,
                             std::unordered_set<std::string> &out) {
    auto eff_vars = get_variables(r.effect);
    auto cond_vars = get_variables(r.conditions);
    out.clear();
    for (const auto &v : eff_vars)
        if (!cond_vars.contains(v)) out.insert(v);
    return !out.empty();
}
}

void Program::normalize() {
    // remove_free_effect_variables
    bool must_add_predicate = false;
    for (auto &r : rules) {
        std::unordered_set<std::string> unbound;
        if (has_unbound_effect_vars(r, unbound)) {
            must_add_predicate = true;
            std::vector<std::string> sorted_unbound(unbound.begin(),
                                                    unbound.end());
            std::ranges::sort(sorted_unbound);
            for (const auto &v : sorted_unbound)
                r.conditions.emplace_back("@object", std::vector<Arg>{Arg(v)});
        }
    }
    if (must_add_predicate) {
        std::cout << "Unbound effect variables: Adding @object predicate."
                  << std::endl;
        // Snapshot objects to avoid invalidating during add_fact.
        std::vector<std::string> objs(objects.begin(), objects.end());
        for (const auto &o : objs)
            add_fact(Atom("@object", std::vector<Arg>{Arg(o)}));
    }
    // split_duplicate_arguments
    bool printed = false;
    for (auto &r : rules) {
        if (r.rename_duplicate_variables() && !printed) {
            std::cout << "Duplicate arguments: Adding equality conditions."
                      << std::endl;
            printed = true;
        }
    }
    // convert_trivial_rules
    std::vector<Rule> remaining;
    remaining.reserve(rules.size());
    bool any_trivial = false;
    for (auto &r : rules) {
        if (r.conditions.empty()) {
            any_trivial = true;
            add_fact(Atom(r.effect.predicate, r.effect.args));
        } else {
            remaining.push_back(std::move(r));
        }
    }
    if (any_trivial)
        std::cout << "Trivial rules: Converted to facts." << std::endl;
    rules = std::move(remaining);
}

void Program::dump(std::ostream &os) const {
    for (const auto &f : facts) os << f << ".\n";
    for (const auto &r : rules) os << "[none] " << r << ".\n";
}
}
