#ifndef H2_MUTEXES_H
#define H2_MUTEXES_H

#include "axiom.h"
#include "mutex_group.h"
#include "operator.h"
#include "state.h"
#include "variable.h"

#include <cassert>
#include <cstdint>
#include <ctime>
#include <unordered_set>
#include <utility>
#include <vector>

enum class Reachability : int8_t {
    SPURIOUS = 0,
    REACHED = 1,
    NOT_REACHED = 2
};

inline constexpr int UNSOLVABLE = -2;
inline constexpr int TIMEOUT = -1;

class Op_h2 {
    void push_pre(
        const std::vector<std::vector<unsigned>> &atom_index, Variable *var,
        int val) {
        if (var->get_level() >= 0) {
            pre.push_back(atom_index[var->get_level()][val]);
        }
    }

    void push_add(
        const std::vector<std::vector<unsigned>> &atom_index, Variable *var,
        int val) {
        if (var->get_level() >= 0) {
            add.push_back(atom_index[var->get_level()][val]);
        }
    }

    void instantiate_operator_backward(
        const Operator &op,
        const std::vector<std::vector<unsigned>> &atom_index,
        const std::vector<std::vector<std::unordered_set<Atom>>>
            &inconsistent_atoms);
    void instantiate_operator_forward(
        const Operator &op,
        const std::vector<std::vector<unsigned>> &atom_index,
        const std::vector<std::vector<std::unordered_set<Atom>>>
            &inconsistent_atoms);

public:
    Op_h2(
        const Operator &op,
        const std::vector<std::vector<unsigned>> &atom_index,
        const std::vector<std::vector<std::unordered_set<Atom>>>
            &inconsistent_atoms,
        bool regression);

    std::vector<unsigned> pre;
    std::vector<unsigned> add;
    std::vector<unsigned> del;
    Reachability triggered;
};

class H2Mutexes {
    int num_vars;
    std::vector<int> domain_sizes;

    std::unordered_set<Atom> static_atoms;
    std::vector<std::vector<bool>> unreachable;
    std::vector<std::vector<std::unordered_set<Atom>>> inconsistent_atoms;

    unsigned num_atoms;
    // Reachability status for atom pairs and individual atoms (diagonal).
    // Stores full upper triangle including diagonal: num_atoms * (num_atoms +
    // 1) / 2.
    std::vector<Reachability> mutex_status;
    std::vector<Op_h2> h2_ops;
    // Per-operator cache: marks atoms already processed
    std::vector<bool> operator_atom_cache;

    std::vector<std::vector<unsigned>> atom_index;
    std::vector<Atom> atom_index_reverse;
    // Precomputed offsets for fast pair indexing
    std::vector<unsigned> atom_pair_offsets;

    Reachability evaluate_atoms(const std::vector<unsigned> &atoms);

    // Helper methods for modular fixpoint computation
    bool apply_operator(unsigned op_id, std::vector<bool> &in_add_or_del);
    void run_fixpoint();
    int collect_mutexes(
        const std::vector<Variable *> &variables,
        std::vector<MutexGroup> &mutexes, const State &initial_state,
        const std::vector<std::pair<Variable *, int>> &goal, bool regression);
    void mark_spurious_operators(std::vector<Operator> &operators);

    unsigned get_atom_pair_id(unsigned atom1_id, unsigned atom2_id) const {
        // Store full upper triangle including diagonal
        // Diagonal entries (atom1_id == atom2_id) represent individual atoms
        // Off-diagonal entries represent atom pairs
        // Use min/max for branchless computation (faster than swap)
        return atom_pair_offsets[std::min(atom1_id, atom2_id)] +
               std::max(atom1_id, atom2_id);
    }

    bool set_unreachable(
        int var, int val, const std::vector<Variable *> &variables,
        const State &initial_state,
        const std::vector<std::pair<Variable *, int>> &goal);

    int limit_seconds;
    clock_t start_time;
    void check_timeout();

    bool init_values_progression(
        const std::vector<Variable *> &variables, const State &initial_state);
    bool init_values_regression(
        const std::vector<std::pair<Variable *, int>> &goal);
    void init_h2_operators(
        const std::vector<Operator> &operators,
        const std::vector<Axiom> &axioms, bool regression);

    void set_atom_not_reached(int atom_id);

    bool check_initial_state_is_dead_end(
        const std::vector<Variable *> &variables,
        const State &initial_state) const;

    bool check_goal_state_is_unreachable(
        const std::vector<std::pair<Variable *, int>> &goal) const;
public:
    explicit H2Mutexes(int t = -1) : limit_seconds(t) {
        if (limit_seconds != -1) {
            start_time = clock();
        }
    }

    int compute(
        const std::vector<Variable *> &variables,
        // Not const because may be detected to be spurious.
        std::vector<Operator> &operators, const std::vector<Axiom> &axioms,
        const State &initial_state,
        const std::vector<std::pair<Variable *, int>> &goal,
        std::vector<MutexGroup> &mutexes, bool regression);

    bool are_mutex(int var1, int val1, int var2, int val2) const {
        if (val1 == -1 || val2 == -1)
            return false;

        if (var1 == var2) // Same variable: mutex iff different value.
            return val1 != val2; // TODO: || unreachable[var1][val1]
        unsigned p1 = atom_index[var1][val1];
        unsigned p2 = atom_index[var2][val2];
        return mutex_status[get_atom_pair_id(p1, p2)] == Reachability::SPURIOUS;
    }

    // Faster version when atom_index values are already known
    bool are_mutex_by_index(unsigned a1, unsigned a2) const {
        return mutex_status[get_atom_pair_id(a1, a2)] == Reachability::SPURIOUS;
    }

    unsigned get_atom_id(int var, int value) const {
        return atom_index[var][value];
    }

    int get_num_variables() const {
        return num_vars;
    }

    int get_num_values(int var) const {
        return domain_sizes[var];
    }

    bool is_unreachable(int var, int value) const {
        return unreachable[var][value];
    }

    int detect_unreachable_atoms(
        const std::vector<Variable *> &variables, const State &initial_state,
        const std::vector<std::pair<Variable *, int>> &goal);

    bool remove_spurious_operators(std::vector<Operator> &operators);
    void set_unreachable_atoms(const std::vector<Variable *> &variables);

    bool initialize(
        const std::vector<Variable *> &variables,
        const std::vector<MutexGroup> &mutexes);
};

// Computes h2 mutexes, and removes every unnecessary variables, operators,
// axioms, initial state and goal.
extern bool compute_h2_mutexes(
    const std::vector<Variable *> &variables, std::vector<Operator> &operators,
    std::vector<Axiom> &axioms, std::vector<MutexGroup> &mutexes,
    State &initial_state, const std::vector<std::pair<Variable *, int>> &goal,
    H2Mutexes &h2, bool disable_bw_h2);

#endif
