#include "model.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace translate::grounding {
namespace {
/*
  Helper: replace argument names in `effect` with their position index,
  and rewrite condition args accordingly. Variables not in the effect are
  left as-is (only allowed for projection rules).
*/
std::pair<Atom, std::vector<Atom>> variables_to_numbers(
    const Atom &effect, const std::vector<Atom> &conditions) {
    std::unordered_map<std::string, int> rename;
    std::vector<Arg> new_eff_args = effect.args;
    for (std::size_t i = 0; i < effect.args.size(); ++i) {
        if (effect.args[i].is_symbol()) {
            const std::string &s = effect.args[i].name();
            if (!s.empty() && s.front() == '?') {
                rename[s] = static_cast<int>(i);
                new_eff_args[i] = Arg(static_cast<int>(i));
            }
        }
    }
    Atom new_effect(effect.predicate, std::move(new_eff_args));
    std::vector<Atom> new_conds;
    new_conds.reserve(conditions.size());
    for (const auto &c : conditions) {
        std::vector<Arg> new_args;
        new_args.reserve(c.args.size());
        for (const auto &a : c.args) {
            if (a.is_symbol()) {
                auto it = rename.find(a.name());
                if (it != rename.end()) {
                    new_args.emplace_back(it->second);
                    continue;
                }
            }
            new_args.push_back(a);
        }
        new_conds.emplace_back(c.predicate, std::move(new_args));
    }
    return {new_effect, std::move(new_conds)};
}

class BuildRule {
public:
    Atom effect;
    std::vector<Atom> conditions;
    virtual ~BuildRule() = default;
    BuildRule(Atom e, std::vector<Atom> c)
        : effect(std::move(e)), conditions(std::move(c)) {}
    /*
      `atom_index` is the position of `new_atom` in the model's `items`
      vector. Join/product rules store that index (4 bytes) rather than
      a copy of the atom's args, and look the args back up via `items`
      in fire(). This keeps the per-rule join indexes from duplicating
      every atom's args -- the dominant remaining memory cost on huge
      groundings (rovers-large-simple: the join index held ~9.7M args
      copies, ~2.7 GB).
    */
    virtual void update_index(const Atom &new_atom, int atom_index,
                              int cond_index) = 0;
    /*
      `enqueue` takes args by rvalue so each emitted atom can be moved
      into the queue's seen-set construction instead of copied. `items`
      is the model vector, used to dereference stored atom indices.
    */
    virtual void fire(const Atom &new_atom, int cond_index,
                      const std::vector<Atom> &items,
                      const std::function<void(const std::string &,
                                               std::vector<Arg> &&)>
                          &enqueue) = 0;

protected:
    // Compute effect args using one new condition match.
    std::vector<Arg> prepare_effect(const Atom &new_atom, int cond_index) {
        std::vector<Arg> eff_args = effect.args;
        const auto &cond = conditions[cond_index];
        for (std::size_t i = 0; i < cond.args.size(); ++i) {
            if (cond.args[i].is_position())
                eff_args[cond.args[i].position()] = new_atom.args[i];
        }
        return eff_args;
    }
};

class ProjectRuleB : public BuildRule {
public:
    using BuildRule::BuildRule;
    void update_index(const Atom &, int, int) override {}
    void fire(const Atom &new_atom, int cond_index,
              const std::vector<Atom> &,
              const std::function<void(const std::string &,
                                       std::vector<Arg> &&)>
                  &enqueue) override {
        auto eff_args = prepare_effect(new_atom, cond_index);
        enqueue(effect.predicate, std::move(eff_args));
    }
};

class JoinRuleB : public BuildRule {
public:
    // Positions of common variable args in each of the two conditions.
    std::array<std::vector<int>, 2> common_positions;
    // For each side: key (tuple of common-arg values) -> list of indices
    // into the model's `items` vector. We dereference items[idx].args in
    // fire(); storing indices instead of args copies keeps the join
    // index from duplicating every atom's args.
    std::array<std::unordered_map<std::string, std::vector<int>>, 2>
        atoms_by_key;

    JoinRuleB(Atom e, std::vector<Atom> c)
        : BuildRule(std::move(e), std::move(c)) {
        const auto &la = conditions[0].args;
        const auto &ra = conditions[1].args;
        std::vector<int> lvars, rvars;
        for (const auto &a : la)
            if (a.is_position()) lvars.push_back(a.position());
        for (const auto &a : ra)
            if (a.is_position()) rvars.push_back(a.position());
        std::ranges::sort(lvars);
        std::ranges::sort(rvars);
        std::vector<int> common;
        std::set_intersection(lvars.begin(), lvars.end(),
                              rvars.begin(), rvars.end(),
                              std::back_inserter(common));
        for (int side = 0; side < 2; ++side) {
            const auto &args = conditions[side].args;
            for (int var : common) {
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (args[i].is_position() && args[i].position() == var) {
                        common_positions[side].push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
        }
    }

    static std::string key_of(const Atom &atom, const std::vector<int> &pos) {
        std::string k;
        for (int p : pos) {
            const Arg &a = atom.args[p];
            if (a.is_symbol()) k.append(a.name());
            else k += std::to_string(a.position());
            k.push_back('\x1f');
        }
        return k;
    }

    void update_index(const Atom &new_atom, int atom_index,
                      int cond_index) override {
        std::string k = key_of(new_atom, common_positions[cond_index]);
        atoms_by_key[cond_index][k].push_back(atom_index);
    }

    void fire(const Atom &new_atom, int cond_index,
              const std::vector<Atom> &items,
              const std::function<void(const std::string &,
                                       std::vector<Arg> &&)>
                  &enqueue) override {
        auto eff_args = prepare_effect(new_atom, cond_index);
        std::string k = key_of(new_atom, common_positions[cond_index]);
        int other = 1 - cond_index;
        auto it = atoms_by_key[other].find(k);
        if (it == atoms_by_key[other].end()) return;
        const auto &other_cond = conditions[other];
        for (int stored_idx : it->second) {
            const auto &stored_args = items[stored_idx].args;
            auto args = eff_args;
            for (std::size_t i = 0; i < other_cond.args.size(); ++i) {
                if (other_cond.args[i].is_position())
                    args[other_cond.args[i].position()] = stored_args[i];
            }
            enqueue(effect.predicate, std::move(args));
        }
    }
};

class ProductRuleB : public BuildRule {
public:
    // Per condition: indices into the model's `items` vector.
    std::vector<std::vector<int>> atoms_by_index;
    int empty_index_count;

    ProductRuleB(Atom e, std::vector<Atom> c)
        : BuildRule(std::move(e), std::move(c)),
          atoms_by_index(conditions.size()),
          empty_index_count(static_cast<int>(conditions.size())) {}

    void update_index([[maybe_unused]] const Atom &new_atom, int atom_index,
                      int cond_index) override {
        if (atoms_by_index[cond_index].empty()) --empty_index_count;
        atoms_by_index[cond_index].push_back(atom_index);
    }

    void fire(const Atom &new_atom, int cond_index,
              const std::vector<Atom> &items,
              const std::function<void(const std::string &,
                                       std::vector<Arg> &&)>
                  &enqueue) override {
        if (empty_index_count > 0) return;
        // Bindings from the new_atom for cond_index already applied via
        // prepare_effect.
        auto eff_args = prepare_effect(new_atom, cond_index);
        // For each other condition, iterate over all atoms; collect bindings.
        std::vector<int> positions;
        for (int p = 0; p < static_cast<int>(conditions.size()); ++p)
            if (p != cond_index) positions.push_back(p);

        // Recurse over positions, building eff_args.
        std::function<void(std::size_t, std::vector<Arg> &)> recurse =
            [&](std::size_t k, std::vector<Arg> &args) {
                if (k == positions.size()) {
                    auto copy = args;
                    enqueue(effect.predicate, std::move(copy));
                    return;
                }
                int p = positions[k];
                const auto &cond = conditions[p];
                for (int stored_idx : atoms_by_index[p]) {
                    const auto &atom_args = items[stored_idx].args;
                    auto next_args = args;
                    for (std::size_t i = 0; i < cond.args.size(); ++i) {
                        if (cond.args[i].is_position())
                            next_args[cond.args[i].position()] = atom_args[i];
                    }
                    recurse(k + 1, next_args);
                }
            };
        recurse(0, eff_args);
    }
};

std::vector<std::unique_ptr<BuildRule>> convert_rules(const Program &prog) {
    std::vector<std::unique_ptr<BuildRule>> result;
    result.reserve(prog.rules.size());
    for (const auto &r : prog.rules) {
        auto [eff, conds] = variables_to_numbers(r.effect, r.conditions);
        switch (r.kind) {
            case RuleKind::JOIN:
                result.push_back(std::make_unique<JoinRuleB>(eff, conds));
                break;
            case RuleKind::PRODUCT:
                result.push_back(std::make_unique<ProductRuleB>(eff, conds));
                break;
            case RuleKind::PROJECT:
                result.push_back(std::make_unique<ProjectRuleB>(eff, conds));
                break;
            default:
                throw std::runtime_error(
                    "Rule has no kind set; was split_rules() called?");
        }
    }
    return result;
}

/*
  Unifier: maps predicate name to a set of (rule_index, cond_index) pairs
  whose condition pattern may match an incoming atom. The pattern is
  determined by the constant arguments at fixed positions. For
  simplicity we do not use a trie here — we test all candidates per
  predicate and check constant arguments. This is O(K * C) per atom (K =
  candidates, C = constants), which is acceptable; the original Python
  trie is an optimization we can revisit if benchmarks show it matters.
*/
struct CondRef {
    int rule_index;
    int cond_index;
    // (position, symbol id) pairs that must match the incoming atom.
    // Comparing interned ids avoids a string compare per constant in the
    // hot unify loop; equal names always intern to the same id.
    std::vector<std::pair<int, int>> constants;
};

class Unifier {
public:
    std::unordered_map<std::string, std::vector<CondRef>> by_predicate;

    void insert(int rule_index, int cond_index, const Atom &condition) {
        CondRef ref;
        ref.rule_index = rule_index;
        ref.cond_index = cond_index;
        for (std::size_t i = 0; i < condition.args.size(); ++i) {
            const Arg &a = condition.args[i];
            if (a.is_symbol()) {
                const std::string &s = a.name();
                if (!s.empty() && s.front() != '?')
                    ref.constants.emplace_back(static_cast<int>(i), a.v);
            }
        }
        by_predicate[condition.predicate].push_back(std::move(ref));
    }

    // Fill `out` with (rule_index, cond_index) matches for `atom`. Caller
    // owns `out` and clears it before each call; this lets the inner
    // vector storage be reused across the entire model build, avoiding
    // an allocation per atom.
    void unify(const Atom &atom,
               std::vector<std::pair<int, int>> &out) const {
        auto it = by_predicate.find(atom.predicate);
        if (it == by_predicate.end()) return;
        for (const auto &ref : it->second) {
            bool ok = true;
            for (const auto &[p, v] : ref.constants) {
                if (p >= static_cast<int>(atom.args.size())) { ok = false; break; }
                const Arg &a = atom.args[p];
                if (!a.is_symbol() || a.v != v) { ok = false; break; }
            }
            if (ok) out.emplace_back(ref.rule_index, ref.cond_index);
        }
    }
};

/*
  Queue of unique atoms (deduplicated via (predicate, args) hash).
*/
/*
  The semi-naive evaluator keeps every derived atom in `items` (this is
  the model we return) and needs a dedup set so each atom is enqueued at
  most once. Storing a *second* full Atom copy in the dedup set doubles
  peak memory, which is the dominant cost on huge groundings -- e.g.
  rovers-large-simple grounds to ~10M atoms and the duplicate storage
  pushed us past 8 GB while the Python translator fits in ~4 GB.

  Instead the dedup set stores 4-byte indices into `items`, with a hash
  and equality that dereference `items`. To test/insert a candidate we
  tentatively append it to `items`, try to insert its index, and roll
  back the append if an equal atom was already present. `items` only
  ever grows (pop just advances `pos`), so stored indices stay valid;
  reallocation moves the buffer but the index->Atom mapping is unchanged.
*/
class AtomQueue {
public:
    std::vector<Atom> items;
    std::size_t pos = 0;
    std::size_t pushes = 0;

private:
    struct IdxHash {
        const std::vector<Atom> *items;
        std::size_t operator()(int i) const noexcept {
            return AtomHash{}((*items)[i]);
        }
    };
    struct IdxEq {
        const std::vector<Atom> *items;
        bool operator()(int a, int b) const noexcept {
            return (*items)[a] == (*items)[b];
        }
    };
    std::unordered_set<int, IdxHash, IdxEq> seen;

    // Append `a` to items, keep it only if not already seen.
    void insert_if_new(Atom &&a) {
        items.push_back(std::move(a));
        int idx = static_cast<int>(items.size()) - 1;
        if (!seen.insert(idx).second)
            items.pop_back();
    }

public:
    explicit AtomQueue(std::vector<Atom> initial)
        : seen(0, IdxHash{&items}, IdxEq{&items}) {
        // Matches Python's `num_pushes = len(atoms)` initial count.
        pushes = initial.size();
        for (auto &a : initial) insert_if_new(std::move(a));
    }
    bool empty() const { return pos >= items.size(); }
    void push(const std::string &pred, std::vector<Arg> &&args) {
        ++pushes; // count every push attempt, like Python's queue.push
        insert_if_new(Atom(pred, std::move(args)));
    }
    Atom pop() { return items[pos++]; }
};
}

std::vector<Atom> compute_model(const Program &prog) {
    std::cout << "Preparing model..." << std::endl;
    auto rules = convert_rules(prog);
    Unifier unifier;
    for (std::size_t i = 0; i < rules.size(); ++i) {
        for (std::size_t k = 0; k < rules[i]->conditions.size(); ++k) {
            unifier.insert(static_cast<int>(i), static_cast<int>(k),
                           rules[i]->conditions[k]);
        }
    }
    std::vector<Atom> fact_atoms = prog.facts;
    std::sort(fact_atoms.begin(), fact_atoms.end());
    AtomQueue queue(std::move(fact_atoms));

    std::cout << "Generated " << rules.size() << " rules." << std::endl;
    std::cout << "Computing model..." << std::endl;
    std::size_t relevant = 0, auxiliary = 0;
    std::vector<std::pair<int, int>> matches;
    while (!queue.empty()) {
        // Index of the atom in queue.items, captured before pop advances.
        int idx = static_cast<int>(queue.pos);
        Atom next = queue.pop();
        if (next.predicate.find('$') != std::string::npos) ++auxiliary;
        else ++relevant;
        matches.clear();
        unifier.unify(next, matches);
        for (const auto &[ri, ci] : matches) {
            rules[ri]->update_index(next, idx, ci);
            rules[ri]->fire(next, ci, queue.items,
                            [&](const std::string &p,
                                std::vector<Arg> &&args) {
                                queue.push(p, std::move(args));
                            });
        }
    }
    std::cout << relevant << " relevant atoms" << std::endl;
    std::cout << auxiliary << " auxiliary atoms" << std::endl;
    std::cout << queue.items.size() << " final queue length" << std::endl;
    std::cout << queue.pushes << " total queue pushes" << std::endl;
    return queue.items;
}
}
