#include "h2_mutexes.h"

#include "helper_functions.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

using namespace std;

// Exception for timeout handling
class TimeoutException : public runtime_error {
public:
    TimeoutException() : runtime_error("h^2 computation timed out") {
    }
};

// Bring Reachability enum values into scope for convenience
using Reachability::NOT_REACHED;
using Reachability::REACHED;
using Reachability::SPURIOUS;

Op_h2::Op_h2(
    const Operator &op, const vector<vector<unsigned>> &atom_index,
    const vector<vector<vector<unsigned>>> &inconsistent_atom_indices,
    bool regression) {
    if (op.is_redundant()) {
        triggered = SPURIOUS;
        return;
    }
    triggered = NOT_REACHED;

    pre.reserve(
        op.get_prevail().size() + op.get_pre_post().size() +
        op.get_augmented_preconditions().size());
    add.reserve(
        op.get_pre_post().size() + op.get_potential_preconditions().size());

    if (regression) {
        instantiate_operator_backward(
            op, atom_index, inconsistent_atom_indices);
    } else {
        instantiate_operator_forward(op, atom_index, inconsistent_atom_indices);
    }

    // Sort pre and add by atom id. run_fixpoint's precondition check assumes
    // pre is in increasing order (so pair offsets need no min/max), and the
    // output is canonical.
    sort(pre.begin(), pre.end());
    sort(add.begin(), add.end());

    // instantiate_operator_* appended (possibly duplicate) del candidates to
    // `del`. Sort + unique them, then drop any atom that is also added.
    sort(del.begin(), del.end());
    del.erase(unique(del.begin(), del.end()), del.end());
    vector<unsigned> kept_del;
    kept_del.reserve(del.size());
    set_difference(
        del.begin(), del.end(), add.begin(), add.end(),
        back_inserter(kept_del));
    del.swap(kept_del);

    if (pre.empty())
        triggered = REACHED;
}

bool compute_h2_mutexes(
    const vector<Variable *> &variables, vector<Operator> &operators,
    vector<Axiom> &axioms, vector<MutexGroup> &mutexes, State &initial_state,
    const vector<pair<Variable *, int>> &goals, H2Mutexes &h2,
    bool disable_bw_h2) {
    if (!h2.initialize(variables, mutexes)) {
        return true;
    }
    int total_mutexes_fw = 0;
    int total_mutexes_bw = 0;

    // h^2 mutexes are loaded and operators disambiguated.
    // Pruning ops may lead to finding more mutexes, which may lead to more
    // spurious states actually not worth it, afaik it only works in nomystery.
    bool update_progression = true;
    bool update_regression = true;
    bool regression = false;
    clock_t start_time = clock();
    int num_iterations = 0;
    do {
        num_iterations++;
        if ((!regression && update_progression) ||
            (regression && update_regression)) {
            if (regression) {
                update_regression = false;
            } else {
                update_progression = false;
            }
            if (!(regression && disable_bw_h2)) {
                cout << "Running " << (regression ? "backward" : "forward")
                     << " mutex detection and operator pruning..." << endl;
                int mutexes_detected;
                try {
                    mutexes_detected = h2.compute(
                        variables, operators, axioms, initial_state, goals,
                        mutexes, regression);
                } catch (const TimeoutException &) {
                    mutexes_detected = TIMEOUT;
                }
                if (mutexes_detected == TIMEOUT) {
                    break;
                } else if (mutexes_detected == UNSOLVABLE) {
                    return false;
                }
                cout << "  Mutexes detected ("
                     << (regression ? "backward" : "forward")
                     << "): " << mutexes_detected << endl;

                if (regression)
                    total_mutexes_bw += mutexes_detected;
                else
                    total_mutexes_fw += mutexes_detected;

                int unreachable_result = 0;
                if (mutexes_detected) {
                    cout << "  Detecting unreachable fluents..." << endl;
                    try {
                        unreachable_result = h2.detect_unreachable_atoms(
                            variables, initial_state, goals);
                    } catch (const TimeoutException &) {
                        unreachable_result = TIMEOUT;
                    }
                    if (unreachable_result == TIMEOUT) {
                        break;
                    } else if (unreachable_result == UNSOLVABLE) {
                        return false;
                    }
                    cout << "  Unreachable fluents found: "
                         << unreachable_result << endl;
                } else {
                    cout
                        << "  Skipping unreachable-fluent detection (no new h2 facts)."
                        << endl;
                }
                bool unreachable_detected = unreachable_result != 0;

                bool spurious_detected = false;
                cout << "  Removing spurious operators..." << endl;
                try {
                    spurious_detected = h2.remove_spurious_operators(operators);
                } catch (const TimeoutException &) {
                    break;
                }
                cout << "  Spurious operators removed." << endl;

                update_progression |= spurious_detected ||
                                      unreachable_detected ||
                                      (regression && mutexes_detected);
                update_regression |= spurious_detected ||
                                     unreachable_detected ||
                                     (!regression && mutexes_detected);
            }
        }
        regression = !regression;
        cout << "Time after iteration " << num_iterations << ": "
             << get_passed_time(start_time) << "s" << endl;
    } while (update_progression || update_regression);

    cout << "Mutex computation completed in " << get_passed_time(start_time)
         << "s (" << num_iterations << " iterations)" << endl;
    cout << "  Forward mutexes: " << total_mutexes_fw << endl;
    cout << "  Backward mutexes: " << total_mutexes_bw << endl;
    return true;
}

int H2Mutexes::detect_unreachable_atoms(
    const vector<Variable *> &variables, const State &initial_state,
    const vector<pair<Variable *, int>> &goals) {
    bool new_unreachable;
    int num_discovered = 0;
    do {
        new_unreachable = false;
        for (int i = 0; i < num_vars; i++) {
            if (domain_sizes[i] - num_unreachable_by_var[i] != 1)
                continue;

            int static_value = -1;
            for (int j = 0; j < domain_sizes[i]; j++) {
                if (!is_unreachable_by_id(atom_index[i][j])) {
                    static_value = j;
                    break;
                }
            }
            // If there is only one possible fluent, this fluent is static.
            if (static_value != -1) {
                unsigned static_atom_id = atom_index[i][static_value];
                // If it was not detected as static before.
                if (!static_atoms[static_atom_id]) {
                    static_atoms[static_atom_id] = 1;

                    // Set inconsistent with everything else.
                    const vector<unsigned> &inconsistent_ids =
                        inconsistent_atom_indices[i][static_value];
                    for (unsigned atom_id : inconsistent_ids) {
                        const Atom &it = atom_index_reverse[atom_id];
                        if (!is_unreachable_by_id(atom_id)) {
                            if (!set_unreachable(
                                    it.var, it.value, variables, initial_state,
                                    goals))
                                return UNSOLVABLE;
                            new_unreachable = true;
                            num_discovered++;
                        }
                    }
                }
            }
        }
    } while (new_unreachable);

    return num_discovered;
}

bool H2Mutexes::set_unreachable(
    int var, int val, const vector<Variable *> &variables,
    const State &initial_state, const vector<pair<Variable *, int>> &goals) {
    if (initial_state[variables[var]] == val)
        return false;
    for (const auto &[goal_var, goal_val] : goals)
        if (goal_var == variables[var] && goal_val == val)
            return false;

    unreachable_atoms[atom_index[var][val]] = 1;
    ++num_unreachable_by_var[var];
    if (variables[var]->is_reachable(val)) {
        cout << "  Marking unreachable: " << variables[var]->get_atom_name(val)
             << endl;
        variables[var]->set_unreachable(val);
    } else {
        cout << "  WARNING: Atom already marked unreachable" << endl;
    }

    set_atom_not_reached(atom_index[var][val]);

    return true;
}

bool H2Mutexes::remove_spurious_operators(vector<Operator> &operators) {
    int count = 0, totalCount = 0;
    bool spurious_detected = false;
    for (size_t i = 0; i < operators.size(); ++i) {
        if (i % 1000 == 0) {
            check_timeout();
        }
        Operator &op = operators[i];
        if (!op.is_redundant()) {
            totalCount++;
            op.remove_ambiguity(*this);
            if (op.is_redundant()) {
                spurious_detected = true;
                count++;
            }
        }
    }
    cout << "  Spurious operators: " << count << " of " << totalCount << endl;
    return spurious_detected;
}

bool H2Mutexes::initialize(
    const vector<Variable *> &variables, const vector<MutexGroup> &mutexes) {
    cout << "Initializing mutex computation..." << endl;
    num_vars = variables.size();
    domain_sizes.resize(num_vars);
    for (int i = 0; i < num_vars; i++) {
        domain_sizes[i] = variables[i]->get_range();
    }

    num_atoms = 0;
    atom_index.resize(num_vars);
    for (int var_id = 0; var_id < num_vars; ++var_id) {
        atom_index[var_id].resize(variables[var_id]->get_range());
        for (int value = 0; value < variables[var_id]->get_range(); ++value) {
            atom_index_reverse.emplace_back(var_id, value);
            atom_index[var_id][value] = num_atoms++;
        }
    }

    static_atoms.assign(num_atoms, 0);
    unreachable_atoms.assign(num_atoms, 0);
    num_unreachable_by_var.assign(num_vars, 0);

    inconsistent_atom_indices.resize(num_vars);
    for (int i = 0; i < num_vars; i++) {
        inconsistent_atom_indices[i].resize(domain_sizes[i]);
        // Each value gets at least the D-1 same-variable mutexes, plus slack
        // for cross-variable ones.
        size_t index_capacity =
            (domain_sizes[i] > 0 ? domain_sizes[i] - 1 : 0) + 8;
        for (int j = 0; j < domain_sizes[i]; ++j)
            inconsistent_atom_indices[i][j].reserve(index_capacity);
    }
    // Initialize everything to NOT_REACHED (mutexes will be set to spurious).
    // Store full upper triangle including diagonal: num_atoms * (num_atoms + 1)
    // / 2 Diagonal entries represent individual atoms, off-diagonal represent
    // pairs.
    size_t mutex_status_size = num_atoms * (num_atoms + 1) / 2;
    // The 4 GiB limit keeps mutex_status_size within the unsigned 32-bit range
    // (so pair IDs fit in unsigned).
    constexpr size_t MAX_MUTEX_STATUS_BYTES = size_t(4) * 1024 * 1024 * 1024;
    if (mutex_status_size > MAX_MUTEX_STATUS_BYTES) {
        cerr << "h^2 mutex table would require " << (mutex_status_size >> 30)
             << " GiB for " << num_atoms
             << " fluents, which exceeds the 4 GiB limit. "
             << "Skipping h^2 mutex computation." << endl;
        return false;
    }
    mutex_status.resize(mutex_status_size, NOT_REACHED);

    // Precompute offsets for fast pair index lookup including diagonal.
    atom_pair_offsets.reserve(num_atoms);
    size_t current_offset = 0;
    for (unsigned atom1 = 0; atom1 < num_atoms; ++atom1) {
        // Offset needs adjustment: subtract atom1 to account for atom2
        // starting at atom1 (including diagonal)
        atom_pair_offsets.push_back(current_offset - atom1);
        // For each atom1, there are (num_atoms - atom1) entries including
        // diagonal
        current_offset += (num_atoms - atom1);
    }
    assert(atom_pair_offsets.size() == num_atoms);

    // Different values of the same variable are always mutex. Record them in
    // inconsistent_atom_indices so the hot paths iterate one combined mutex
    // list. (Cross-variable mutexes are added later from the input mutex
    // groups and during collect_mutexes.)
    for (int var = 0; var < num_vars; ++var) {
        for (int val1 = 0; val1 < domain_sizes[var]; ++val1) {
            int atom1_id = atom_index[var][val1];
            for (int val2 = val1 + 1; val2 < domain_sizes[var]; ++val2) {
                int atom2_id = atom_index[var][val2];
                mutex_status[get_atom_pair_id(atom1_id, atom2_id)] = SPURIOUS;
                inconsistent_atom_indices[var][val1].push_back(atom2_id);
                inconsistent_atom_indices[var][val2].push_back(atom1_id);
            }
        }
    }

    for (const MutexGroup &mutex : mutexes) {
        vector<Atom> invariant_group = mutex.get_mutex_group();
        for (size_t j = 0; j < invariant_group.size(); ++j) {
            const Atom &atom1 = invariant_group[j];
            if (atom1.var == -1)
                continue;
            for (const Atom &atom2 : invariant_group) {
                if (atom2.var == -1)
                    continue;
                if (atom1.var != atom2.var) {
                    /* The "different variable" test makes sure we
                       don't mark an atom as mutex with itself
                       (important for correctness) and don't include
                       redundant mutexes (important to conserve
                       memory). Note that the preprocessor removes
                       mutex groups that contain *only* redundant
                       mutexes, but it can of course generate mutex
                       groups which lead to *some* redundant mutexes,
                       where some but not all atoms talk about the
                       same variable. */
                    unsigned atom1_id = atom_index[atom1.var][atom1.value];
                    unsigned atom2_id = atom_index[atom2.var][atom2.value];
                    // Use mutex_status as the dedup oracle: a pair may appear
                    // in several mutex groups, so only record it the first
                    // time.
                    unsigned pair = get_atom_pair_id(atom1_id, atom2_id);
                    if (mutex_status[pair] != SPURIOUS) {
                        inconsistent_atom_indices[atom1.var][atom1.value]
                            .push_back(atom2_id);
                        inconsistent_atom_indices[atom2.var][atom2.value]
                            .push_back(atom1_id);
                        mutex_status[pair] = SPURIOUS;
                    }
                }
            }
        }
    }

    cout << "Initialized h^2 mutex computation with " << num_atoms << " fluents"
         << endl;
    return true;
}

bool H2Mutexes::init_values_progression(
    const vector<Variable *> &variables, const State &initial_state) {
    size_t num_spurious = 0, num_reached = 0, num_not_reached = 0;

    for (Reachability &status : mutex_status) {
        if (status == SPURIOUS) {
            num_spurious++;
            continue;
        }
        status = NOT_REACHED;
        num_not_reached++;
    }

    // Pre-compute fluent indices to avoid repeated lookups
    vector<unsigned> initial_fluents;
    initial_fluents.reserve(variables.size());
    for (unsigned i = 0; i < variables.size(); i++) {
        int var = variables[i]->get_level();
        initial_fluents.push_back(atom_index[var][initial_state[variables[i]]]);
    }

    // Update pairs and individual atoms with optimal cache locality
    // Process all entries involving each fluent sequentially
    for (unsigned i = 0; i < initial_fluents.size(); i++) {
        unsigned fluent1 = initial_fluents[i];
        // Mark individual atom as reached (diagonal entry)
        unsigned diag_pos = get_atom_pair_id(fluent1, fluent1);
        if (mutex_status[diag_pos] == NOT_REACHED) {
            mutex_status[diag_pos] = REACHED;
            num_reached++;
            num_not_reached--;
        }
        // Process all pairs with fluent1 in a cache-friendly order
        for (unsigned j = i + 1; j < initial_fluents.size(); j++) {
            unsigned fluent2 = initial_fluents[j];
            unsigned pos = get_atom_pair_id(fluent1, fluent2);
            if (mutex_status[pos] == SPURIOUS)
                return false;
            if (mutex_status[pos] == NOT_REACHED) {
                mutex_status[pos] = REACHED;
                num_reached++;
                num_not_reached--;
            }
        }
    }
    cout << "Forward reachability initialized: " << num_reached << " reached, "
         << num_not_reached << " not reached, " << num_spurious << " spurious"
         << endl;

    return true;
}

bool H2Mutexes::check_goal_state_is_unreachable(
    const vector<pair<Variable *, int>> &goal) const {
    // Pre-compute goal fluent indices once
    vector<unsigned> goal_fluents;
    goal_fluents.reserve(goal.size());
    for (const auto &[goal_var, goal_val] : goal) {
        int var = goal_var->get_level();
        goal_fluents.push_back(atom_index[var][goal_val]);
    }

    // Check with cached position calculations
    for (unsigned i = 0; i < goal_fluents.size(); i++) {
        unsigned fluent1 = goal_fluents[i];
        for (unsigned j = 0; j < goal_fluents.size(); j++) {
            if (i == j)
                continue;
            unsigned pos = get_atom_pair_id(fluent1, goal_fluents[j]);
            if (mutex_status[pos] == SPURIOUS) {
                return true;
            }
        }
    }
    return false;
}

bool H2Mutexes::init_values_regression(
    const vector<pair<Variable *, int>> &goal) {
    cout << "Initializing backward reachability..." << endl;

    if (check_goal_state_is_unreachable(goal))
        return false;

    for (Reachability &status : mutex_status) {
        if (status != SPURIOUS) {
            status = REACHED;
        }
    }

    // Collect all atom IDs that should be NOT_REACHED, then apply them in one
    // sequential sweep through mutex_status below. The sweep visits every pair
    // (~num_atoms^2/2 entries), which is more work than touching just the
    // affected pairs, but it stays cache-friendly instead of scattering random
    // writes across mutex_status -- and a full scan over mutex_status follows
    // immediately anyway.
    vector<bool> should_be_not_reached(num_atoms, false);
    bool any_not_reached = false;

    for (const auto &[var_ptr, val] : goal) {
        int goal_var = var_ptr->get_level();
        int goal_val = val;

        for (unsigned aid : inconsistent_atom_indices[goal_var][goal_val]) {
            if (!should_be_not_reached[aid]) {
                should_be_not_reached[aid] = true;
                any_not_reached = true;
            }
        }
        for (int val1 = 0; val1 < domain_sizes[goal_var]; val1++) {
            if (val1 != goal_val) {
                unsigned aid = atom_index[goal_var][val1];
                if (!should_be_not_reached[aid]) {
                    should_be_not_reached[aid] = true;
                    any_not_reached = true;
                }
            }
        }
    }

    if (any_not_reached) {
        // Single sequential sweep through mutex_status to apply all not-reached
        // markers
        for (unsigned atom1 = 0; atom1 < num_atoms; atom1++) {
            bool snr1 = should_be_not_reached[atom1];
            // Diagonal entry
            unsigned diag = get_atom_pair_id(atom1, atom1);
            if (snr1 && mutex_status[diag] == REACHED)
                mutex_status[diag] = NOT_REACHED;
            // Off-diagonal entries (atom1, atom2) for atom2 > atom1
            unsigned pair_id = atom_pair_offsets[atom1] + atom1 + 1;
            for (unsigned atom2 = atom1 + 1; atom2 < num_atoms;
                 atom2++, pair_id++) {
                if ((snr1 || should_be_not_reached[atom2]) &&
                    mutex_status[pair_id] == REACHED) {
                    mutex_status[pair_id] = NOT_REACHED;
                }
            }
        }
    }

    size_t num_spurious = 0, num_reached = 0, num_not_reached = 0;
    for (const Reachability &status : mutex_status) {
        if (status == REACHED) {
            num_reached++;
        } else if (status == NOT_REACHED) {
            num_not_reached++;
        } else {
            num_spurious++;
        }
    }

    cout << "Backward reachability initialized: " << num_reached << " reached, "
         << num_not_reached << " not reached, " << num_spurious << " spurious"
         << endl;

    return true;
}

void H2Mutexes::set_atom_not_reached(int atom_id) {
    // Mark the individual atom as not reached (diagonal entry)
    unsigned diag_pos = get_atom_pair_id(atom_id, atom_id);
    if (mutex_status[diag_pos] == REACHED) {
        mutex_status[diag_pos] = NOT_REACHED;
    }
    // Update all pairs involving this atom
    for (unsigned other_atom = 0; other_atom < num_atoms; other_atom++) {
        if (other_atom == static_cast<unsigned>(atom_id))
            continue;
        unsigned pos = get_atom_pair_id(atom_id, other_atom);
        if (mutex_status[pos] == REACHED) {
            mutex_status[pos] = NOT_REACHED;
        }
    }
}

void H2Mutexes::init_h2_operators(
    const vector<Operator> &operators, const vector<Axiom> &axioms,
    bool regression) {
    h2_ops.clear();
    h2_ops.reserve(operators.size());
    for (size_t i = 0; i < operators.size(); ++i) {
        if (i % 1000 == 0) {
            check_timeout();
        }
        h2_ops.emplace_back(
            operators[i], atom_index, inconsistent_atom_indices, regression);
    }

    if (!axioms.empty()) {
        cerr << "Error, axioms not supported by h2" << endl;
        exit(1);
    }
}

// run_fixpoint inlines the operator application logic for better delta
// tracking.

// Run the fixpoint computation to determine reachable atom pairs.
// Throws TimeoutException if time limit exceeded.
void H2Mutexes::run_fixpoint() {
    // Precompute which atoms are individually reached (diagonal entry)
    // and maintain a list of reached atom IDs for efficient iteration.
    vector<unsigned> reached_atoms;
    reached_atoms.reserve(num_atoms);
    for (unsigned a = 0; a < num_atoms; a++) {
        if (mutex_status[atom_pair_offsets[a] + a] == REACHED) {
            reached_atoms.push_back(a);
        }
    }

    vector<uint8_t> in_add_or_del(num_atoms, 0);

    // Track per-operator: was it already triggered in a previous iteration?
    vector<uint8_t> was_triggered(h2_ops.size(), 0);
    // Per-operator: index into reached_atoms up to which atoms have been
    // checked
    vector<size_t> op_checked_up_to(h2_ops.size(), 0);
    // Per-operator: list of atoms that failed precondition check, and for each
    // the precondition atom that blocked it (mutex_status[blocker, atom] was
    // not REACHED). mutex_status is monotone, so on recheck we first test just
    // that one pair (O(1)) and only re-scan all preconditions if it flipped.
    vector<vector<unsigned>> op_failed_atoms(h2_ops.size());
    vector<vector<unsigned>> op_failed_blocker(h2_ops.size());

    bool updated;
    do {
        updated = false;
        for (unsigned op_id = 0; op_id < h2_ops.size(); op_id++) {
            if (op_id % 1000 == 0) {
                check_timeout();
            }

            if (h2_ops[op_id].triggered == SPURIOUS)
                continue;

            // Check if preconditions are met. Inline the check so diagonal
            // atom reachability can use the local is_reached array instead of
            // re-reading diagonal entries from mutex_status.
            bool was_already_triggered = was_triggered[op_id];
            const vector<unsigned> &op_pre = h2_ops[op_id].pre;
            if (h2_ops[op_id].triggered != REACHED) {
                bool pre_reached = true;
                for (unsigned pre_i = 0; pre_reached && pre_i < op_pre.size();
                     pre_i++) {
                    unsigned atom_i = op_pre[pre_i];
                    unsigned atom_i_row = atom_pair_offsets[atom_i];
                    if (mutex_status[atom_i_row + atom_i] != REACHED) {
                        pre_reached = false;
                        break;
                    }
                    for (unsigned pre_j = pre_i + 1;
                         pre_reached && pre_j < op_pre.size(); pre_j++) {
                        pre_reached =
                            (mutex_status[atom_i_row + op_pre[pre_j]] ==
                             REACHED);
                    }
                }
                if (!pre_reached)
                    continue;
                h2_ops[op_id].triggered = REACHED;
            }

            // Skip if nothing to do
            if (was_already_triggered &&
                op_checked_up_to[op_id] >= reached_atoms.size() &&
                op_failed_atoms[op_id].empty()) {
                continue;
            }

            was_triggered[op_id] = 1;

            const vector<unsigned> &op_add = h2_ops[op_id].add;
            const vector<unsigned> &op_del = h2_ops[op_id].del;
            // Returns the first precondition atom whose pair with atom_i is not
            // REACHED (the "blocker"), or ~0u if all preconditions are met.
            const auto find_blocker = [&](unsigned atom_i) -> unsigned {
                unsigned atom_i_row = atom_pair_offsets[atom_i];
                for (unsigned pre_atom : op_pre) {
                    unsigned pos = (pre_atom < atom_i)
                                       ? (atom_pair_offsets[pre_atom] + atom_i)
                                       : (atom_i_row + pre_atom);
                    if (mutex_status[pos] != REACHED)
                        return pre_atom;
                }
                return ~0u;
            };
            const auto mark_pairs_with_add = [&](unsigned atom_i) {
                unsigned atom_i_row = atom_pair_offsets[atom_i];
                for (unsigned p : op_add) {
                    if (atom_i == p)
                        continue;
                    unsigned pos = (p < atom_i)
                                       ? (atom_pair_offsets[p] + atom_i)
                                       : (atom_i_row + p);
                    if (mutex_status[pos] == NOT_REACHED) {
                        mutex_status[pos] = REACHED;
                        updated = true;
                    }
                }
            };

            // Build lookup table for O(1) membership test
            for (unsigned atom_id : op_add)
                in_add_or_del[atom_id] = 1;
            for (unsigned atom_id : op_del)
                in_add_or_del[atom_id] = 1;

            // First pass: update individual atoms and pairs within add effects
            for (unsigned add_i = 0; add_i < op_add.size(); add_i++) {
                unsigned p = op_add[add_i];
                unsigned p_row = atom_pair_offsets[p];
                unsigned diag_p = p_row + p;
                if (mutex_status[diag_p] == NOT_REACHED) {
                    mutex_status[diag_p] = REACHED;
                    reached_atoms.push_back(p);
                    updated = true;
                }
                for (unsigned add_j = add_i + 1; add_j < op_add.size();
                     add_j++) {
                    unsigned q = op_add[add_j];
                    unsigned pos_pq = p_row + q;
                    if (mutex_status[pos_pq] == NOT_REACHED) {
                        mutex_status[pos_pq] = REACHED;
                        updated = true;
                    }
                }
            }

            if (!was_already_triggered) {
                // Newly triggered: check all reached atoms
                vector<unsigned> new_failed;
                vector<unsigned> new_blocker;
                for (size_t ri = 0; ri < reached_atoms.size(); ri++) {
                    unsigned atom_i = reached_atoms[ri];
                    if (in_add_or_del[atom_i])
                        continue;

                    unsigned blk = find_blocker(atom_i);
                    if (blk == ~0u) {
                        mark_pairs_with_add(atom_i);
                    } else {
                        new_failed.push_back(atom_i);
                        new_blocker.push_back(blk);
                    }
                }
                op_checked_up_to[op_id] = reached_atoms.size();
                op_failed_atoms[op_id] = std::move(new_failed);
                op_failed_blocker[op_id] = std::move(new_blocker);
            } else {
                // Already triggered: re-check failed atoms + check new atoms
                vector<unsigned> &failed = op_failed_atoms[op_id];
                vector<unsigned> &blocker = op_failed_blocker[op_id];
                size_t write_idx = 0;
                for (size_t fi = 0; fi < failed.size(); fi++) {
                    unsigned atom_i = failed[fi];
                    if (in_add_or_del[atom_i])
                        continue;

                    // Fast path: if the cached blocking pair is still not
                    // REACHED, atom_i is still blocked (mutex_status monotone).
                    unsigned b = blocker[fi];
                    unsigned bpos = (b < atom_i)
                                        ? (atom_pair_offsets[b] + atom_i)
                                        : (atom_pair_offsets[atom_i] + b);
                    if (mutex_status[bpos] != REACHED) {
                        failed[write_idx] = atom_i;
                        blocker[write_idx] = b;
                        write_idx++;
                        continue;
                    }

                    unsigned blk = find_blocker(atom_i);
                    if (blk == ~0u) {
                        mark_pairs_with_add(atom_i);
                    } else {
                        failed[write_idx] = atom_i;
                        blocker[write_idx] = blk;
                        write_idx++;
                    }
                }
                failed.resize(write_idx);
                blocker.resize(write_idx);

                size_t start = op_checked_up_to[op_id];
                for (size_t ri = start; ri < reached_atoms.size(); ri++) {
                    unsigned atom_i = reached_atoms[ri];
                    if (in_add_or_del[atom_i])
                        continue;

                    unsigned blk = find_blocker(atom_i);
                    if (blk == ~0u) {
                        mark_pairs_with_add(atom_i);
                    } else {
                        failed.push_back(atom_i);
                        blocker.push_back(blk);
                    }
                }
                op_checked_up_to[op_id] = reached_atoms.size();
            }

            // Reset lookup table
            for (unsigned atom_id : op_add)
                in_add_or_del[atom_id] = 0;
            for (unsigned atom_id : op_del)
                in_add_or_del[atom_id] = 0;
        }
        check_timeout();
    } while (updated);
}

// Mark operators that were never triggered as spurious.
void H2Mutexes::mark_spurious_operators(vector<Operator> &operators) {
    int num_spurious_ops = 0;
    for (unsigned op_id = 0; op_id < h2_ops.size(); op_id++) {
        if (h2_ops[op_id].triggered == NOT_REACHED) {
            num_spurious_ops++;
            operators[op_id].set_spurious();
        }
    }
    cout << "  Operators not triggered: " << num_spurious_ops << endl;
}

// Collect mutexes and unreachable atoms after fixpoint computation.
// Returns the count of new mutexes and unreachable atoms, or UNSOLVABLE.
// Throws TimeoutException if time limit exceeded.
int H2Mutexes::collect_mutexes(
    const vector<Variable *> &variables, vector<MutexGroup> &mutexes,
    const State &initial_state, const vector<pair<Variable *, int>> &goal,
    bool regression) {
    unsigned count = 0;
    int num_unreachable = 0;

    // Collect mutexes and unreachable atoms in a single pass for better cache
    // locality. Process each atom's diagonal entry and all its pairs
    // contiguously.
    for (unsigned atom1_id = 0; atom1_id < num_atoms; atom1_id++) {
        // First check diagonal entry (individual atom reachability).
        unsigned diag_pos = get_atom_pair_id(atom1_id, atom1_id);
        if (mutex_status[diag_pos] == NOT_REACHED) {
            Atom atom = atom_index_reverse[atom1_id];
            if (!is_unreachable_by_id(atom1_id)) {
                num_unreachable++;
                if (!set_unreachable(
                        atom.var, atom.value, variables, initial_state, goal)) {
                    return UNSOLVABLE;
                }
            }
        }

        // Then process all pairs with this atom (cache-friendly sequential
        // access). Compute indices incrementally: starting from position
        // atom_pair_offsets[atom1_id] + atom1_id + 1, each iteration increments
        // by 1.
        bool atom1_reached = (mutex_status[diag_pos] == REACHED);
        unsigned pair_id = atom_pair_offsets[atom1_id] + atom1_id + 1;
        for (unsigned atom2_id = atom1_id + 1; atom2_id < num_atoms;
             atom2_id++, pair_id++) {
            if (mutex_status[pair_id] == NOT_REACHED) {
                mutex_status[pair_id] = SPURIOUS;

                // Only create mutex if both atoms are individually reachable.
                if (atom1_reached) {
                    unsigned diag2 = get_atom_pair_id(atom2_id, atom2_id);
                    if (mutex_status[diag2] == REACHED) {
                        Atom atom1 = atom_index_reverse[atom1_id];
                        Atom atom2 = atom_index_reverse[atom2_id];
                        ++count;
                        if (atom1.var < atom2.var) {
                            vector<Atom> mut_group{atom1, atom2};
                            mutexes.emplace_back(
                                mut_group, variables, regression);
                        }
                        // This pair transitions NOT_REACHED->SPURIOUS exactly
                        // once, so no dedup is needed.
                        inconsistent_atom_indices[atom1.var][atom1.value]
                            .push_back(atom2_id);
                        inconsistent_atom_indices[atom2.var][atom2.value]
                            .push_back(atom1_id);
                    }
                }
            }
        }
    }

    cout << (regression ? "Backward" : "Forward")
         << " h^2 mutexes added: " << count << " (" << num_unreachable
         << " unreachable atoms)" << endl;

    return count + num_unreachable;
}

// Returns the number of new mutexes or -1 if failed
int H2Mutexes::compute(
    const vector<Variable *> &variables, vector<Operator> &operators,
    const vector<Axiom> &axioms, const State &initial_state,
    const vector<pair<Variable *, int>> &goal, vector<MutexGroup> &mutexes,
    bool regression) {
    cout << "Initializing " << (regression ? "backward" : "forward")
         << " reachability matrix..." << endl;
    if (regression) {
        if (!init_values_regression(goal))
            return UNSOLVABLE;
    } else {
        if (!init_values_progression(variables, initial_state))
            return UNSOLVABLE;
    }

    cout << "Initializing " << (regression ? "backward" : "forward")
         << " operators..." << endl;
    init_h2_operators(operators, axioms, regression);

    cout << "Computing " << (regression ? "backward" : "forward")
         << " h^2 mutexes..." << endl;

    // Initialize operator-atom cache (no longer used - delta tracking replaces
    // it)

    // Run fixpoint computation (may throw TimeoutException)
    run_fixpoint();

    // Report statistics
    int num_reached = 0, num_not_reached = 0, num_spurious = 0;
    for (unsigned i = 0; i < mutex_status.size(); i++) {
        if (mutex_status[i] == REACHED) {
            num_reached++;
        } else if (mutex_status[i] == NOT_REACHED) {
            num_not_reached++;
        } else {
            num_spurious++;
        }
    }
    cout << "Mutex computation completed: " << num_reached << " reached, "
         << num_not_reached << " not reached, " << num_spurious << " spurious"
         << endl;

    // Mark spurious operators
    mark_spurious_operators(operators);

    // Collect mutexes and unreachable atoms
    int new_mutexes =
        collect_mutexes(variables, mutexes, initial_state, goal, regression);
    if (new_mutexes == UNSOLVABLE)
        return UNSOLVABLE;

    return new_mutexes;
}

void H2Mutexes::check_timeout() {
    if (limit_seconds == -1) // no limit
        return;

    double passed_seconds = get_passed_time(start_time);
    if (passed_seconds > limit_seconds) {
        cout << "h^2 mutex computation timed out after " << passed_seconds
             << "s" << endl;
        throw TimeoutException();
    }
}

void Op_h2::instantiate_operator_forward(
    const Operator &op, const vector<vector<unsigned>> &atom_index,
    const vector<vector<vector<unsigned>>> &inconsistent_atom_indices) {
    vector<bool> prepost_var(atom_index.size(), false);

    const vector<Operator::Prevail> &prevail = op.get_prevail();
    for (unsigned j = 0; j < prevail.size(); j++)
        push_pre(atom_index, prevail[j].var, prevail[j].prev);

    const vector<Operator::PrePost> &pre_post = op.get_pre_post();
    for (unsigned j = 0; j < pre_post.size(); j++) {
        if (pre_post[j].pre != -1) {
            push_pre(atom_index, pre_post[j].var, pre_post[j].pre);
        }
        push_add(atom_index, pre_post[j].var, pre_post[j].post);
        prepost_var[pre_post[j].var->get_level()] = true;
    }
    for (unsigned j = 0; j < prevail.size(); j++) {
        int var = prevail[j].var->get_level();
        int prev = prevail[j].prev;
        if (var == -1)
            continue;

        del.insert(
            del.end(), inconsistent_atom_indices[var][prev].begin(),
            inconsistent_atom_indices[var][prev].end());
    }

    for (unsigned j = 0; j < pre_post.size(); j++) {
        int var = pre_post[j].var->get_level();
        int post = pre_post[j].post;

        if (pre_post[j].is_conditional_effect)
            continue;
        if (var == -1)
            continue;

        del.insert(
            del.end(), inconsistent_atom_indices[var][post].begin(),
            inconsistent_atom_indices[var][post].end());
    }

    const vector<Atom> &augmented = op.get_augmented_preconditions();
    for (const Atom &atom : augmented) {
        int var = atom.var;
        int val = atom.value;
        unsigned atom_id = atom_index[var][val];
        pre.push_back(atom_id);

        if (!prepost_var[var]) {
            del.insert(
                del.end(), inconsistent_atom_indices[var][val].begin(),
                inconsistent_atom_indices[var][val].end());
        }
    }
}

void Op_h2::instantiate_operator_backward(
    const Operator &op, const vector<vector<unsigned>> &atom_index,
    const vector<vector<vector<unsigned>>> &inconsistent_atom_indices) {
    vector<bool> prepost_var(atom_index.size(), false);

    const vector<Operator::Prevail> &prevail = op.get_prevail();
    for (unsigned j = 0; j < prevail.size(); j++)
        push_pre(atom_index, prevail[j].var, prevail[j].prev);

    const vector<Operator::PrePost> &pre_post = op.get_pre_post();
    for (unsigned j = 0; j < pre_post.size(); j++) {
        if (pre_post[j].pre != -1)
            push_add(atom_index, pre_post[j].var, pre_post[j].pre);

        if (!pre_post[j].is_conditional_effect) {
            push_pre(atom_index, pre_post[j].var, pre_post[j].post);
            prepost_var[pre_post[j].var->get_level()] = true;
        }

        if (pre_post[j].is_conditional_effect) {
            vector<Operator::EffCond> effect_conds = pre_post[j].effect_conds;
            for (unsigned k = 0; k < effect_conds.size(); k++)
                push_add(atom_index, effect_conds[k].var, effect_conds[k].cond);
        }
    }

    for (unsigned j = 0; j < prevail.size(); j++) {
        int var = prevail[j].var->get_level();
        int prev = prevail[j].prev;
        if (var == -1)
            continue;

        del.insert(
            del.end(), inconsistent_atom_indices[var][prev].begin(),
            inconsistent_atom_indices[var][prev].end());
    }

    for (size_t j = 0; j < pre_post.size(); j++) {
        if (pre_post[j].is_conditional_effect)
            continue;
        int var = pre_post[j].var->get_level();
        int pre = pre_post[j].pre;
        if (var == -1 || pre == -1)
            continue;

        del.insert(
            del.end(), inconsistent_atom_indices[var][pre].begin(),
            inconsistent_atom_indices[var][pre].end());
    }

    const vector<Atom> &augmented = op.get_augmented_preconditions();
    for (const Atom &atom : augmented) {
        int var = atom.var;
        int val = atom.value;
        if (!prepost_var[var]) {
            unsigned atom_id = atom_index[var][val];
            pre.push_back(atom_id);
        }

        del.insert(
            del.end(), inconsistent_atom_indices[var][val].begin(),
            inconsistent_atom_indices[var][val].end());
    }

    // Potential preconditions from the disambiguation. For values of the same
    // variable, only atoms mutex with *all* candidates are guaranteed deleted,
    // so we take the per-variable intersection of their mutex lists.
    const vector<Atom> &potential = op.get_potential_preconditions();
    if (!potential.empty()) {
        // Group potential preconditions by variable
        unordered_map<int, vector<int>> potential_by_var;
        for (const Atom &atom : potential) {
            potential_by_var[atom.var].push_back(atom.value);
            unsigned atom_id = atom_index[atom.var][atom.value];
            add.push_back(atom_id);
        }

        // For each variable with potential preconditions, compute the
        // intersection of delete sets using sparse dirty lists instead of
        // scanning all atoms.
        unsigned total_atoms = 0;
        for (const auto &var_atoms : atom_index)
            total_atoms += var_atoms.size();
        thread_local vector<uint8_t> pd_bits;
        thread_local vector<uint8_t> pd_intersect;
        thread_local vector<unsigned> pd_list;
        thread_local vector<unsigned> pd_intersect_dirty;
        if (pd_bits.size() < total_atoms) {
            pd_bits.resize(total_atoms, false);
            pd_intersect.resize(total_atoms, false);
        }

        for (const auto &[var, vals] : potential_by_var) {
            pd_list.clear();
            for (size_t vi = 0; vi < vals.size(); ++vi) {
                int val = vals[vi];
                if (vi == 0) {
                    // Initialize from the combined mutex list (cross-var +
                    // same-var).
                    for (unsigned idx : inconsistent_atom_indices[var][val]) {
                        if (!pd_bits[idx]) {
                            pd_bits[idx] = true;
                            pd_list.push_back(idx);
                        }
                    }
                } else {
                    // Mark second set, then filter the current intersection
                    // list.
                    for (unsigned idx : inconsistent_atom_indices[var][val]) {
                        if (!pd_intersect[idx]) {
                            pd_intersect[idx] = true;
                            pd_intersect_dirty.push_back(idx);
                        }
                    }

                    size_t write_idx = 0;
                    for (unsigned idx : pd_list) {
                        if (pd_intersect[idx]) {
                            pd_list[write_idx++] = idx;
                        } else {
                            pd_bits[idx] = false;
                        }
                    }
                    pd_list.resize(write_idx);
                    for (unsigned idx : pd_intersect_dirty)
                        pd_intersect[idx] = false;
                    pd_intersect_dirty.clear();
                }
            }

            // Append the intersection to del and clean up pd_bits.
            for (unsigned idx : pd_list) {
                del.push_back(idx);
                pd_bits[idx] = false;
            }
        }
    }
}
