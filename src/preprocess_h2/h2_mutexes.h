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
            unsigned atom = atom_index[var->get_level()][val];
            pre.push_back(atom);
        }
    }

    void push_add(
        const std::vector<std::vector<unsigned>> &atom_index, Variable *var,
        int val) {
        if (var->get_level() >= 0) {
            unsigned atom = atom_index[var->get_level()][val];
            add.push_back(atom);
        }
    }

    // Both append (possibly duplicate) del candidates to `del`; the Op_h2
    // constructor then sorts, uniques, and removes add atoms.
    void instantiate_operator_backward(
        const Operator &op,
        const std::vector<std::vector<unsigned>> &atom_index,
        const std::vector<std::vector<std::vector<unsigned>>>
            &inconsistent_atom_indices);
    void instantiate_operator_forward(
        const Operator &op,
        const std::vector<std::vector<unsigned>> &atom_index,
        const std::vector<std::vector<std::vector<unsigned>>>
            &inconsistent_atom_indices);

public:
    Op_h2(
        const Operator &op,
        const std::vector<std::vector<unsigned>> &atom_index,
        const std::vector<std::vector<std::vector<unsigned>>>
            &inconsistent_atom_indices,
        bool regression);

    std::vector<unsigned> pre;
    std::vector<unsigned> add;
    std::vector<unsigned> del;
    Reachability triggered;
};

class H2Mutexes {
    int num_vars;
    std::vector<int> domain_sizes;

    std::vector<uint8_t> static_atoms;
    std::vector<uint8_t> unreachable_atoms;
    std::vector<int> num_unreachable_by_var;
    // Per-atom mutex lists as atom indices, iterated by the hot paths
    // (same-variable + cross-variable mutexes).
    std::vector<std::vector<std::vector<unsigned>>> inconsistent_atom_indices;

    size_t num_atoms;
    // Reachability status for atom pairs and individual atoms (diagonal).
    // Stores full upper triangle including diagonal: num_atoms * (num_atoms +
    // 1) / 2.
    std::vector<Reachability> mutex_status;
    std::vector<Op_h2> h2_ops;
    // Per-operator cache removed — delta tracking handles this in run_fixpoint

    std::vector<std::vector<unsigned>> atom_index;
    std::vector<Atom> atom_index_reverse;
    // Precomputed offsets for fast pair indexing
    std::vector<unsigned> atom_pair_offsets;

    // Helper methods for modular fixpoint computation
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

    // Fast path for callers that already know the atom ids are ordered.
    bool are_mutex_by_ordered_index(unsigned a1, unsigned a2) const {
        assert(a1 <= a2);
        return mutex_status[atom_pair_offsets[a1] + a2] ==
               Reachability::SPURIOUS;
    }

    // Get the list of atom indices that are mutex with (var, value)
    // Used for building "blocked" bitsets in disambiguation
    const std::vector<unsigned> &get_mutex_indices(int var, int value) const {
        return inconsistent_atom_indices[var][value];
    }

    unsigned get_atom_id(int var, int value) const {
        return atom_index[var][value];
    }

    int get_atom_var_by_id(unsigned atom_id) const {
        return atom_index_reverse[atom_id].var;
    }

    int get_num_unreachable_values(int var) const {
        return num_unreachable_by_var[var];
    }

    int get_num_variables() const {
        return num_vars;
    }

    int get_num_values(int var) const {
        return domain_sizes[var];
    }

    int get_num_atoms() const {
        return num_atoms;
    }

    bool is_unreachable_by_id(unsigned atom_id) const {
        return unreachable_atoms[atom_id];
    }

    int detect_unreachable_atoms(
        const std::vector<Variable *> &variables, const State &initial_state,
        const std::vector<std::pair<Variable *, int>> &goal);

    bool remove_spurious_operators(std::vector<Operator> &operators);

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
