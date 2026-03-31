#include "h2_mutexes.h"

#include "helper_functions.h"

#include <algorithm>
#include <iterator>
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
    const vector<vector<unordered_set<Atom>>> &inconsistent_atoms,
    bool regression) {
    if (op.is_redundant()) {
        triggered = SPURIOUS;
    } else {
        triggered = NOT_REACHED;
    }

    if (regression) {
        instantiate_operator_backward(op, atom_index, inconsistent_atoms);
    } else {
        instantiate_operator_forward(op, atom_index, inconsistent_atoms);
    }

    sort(pre.begin(), pre.end());
    sort(add.begin(), add.end());
    sort(del.begin(), del.end());

    // Remove add atoms from delete list.
    vector<unsigned int> temp_deletes;
    set_difference(
        del.begin(), del.end(), add.begin(), add.end(),
        back_inserter(temp_deletes));
    del.swap(temp_deletes);
    sort(del.begin(), del.end());
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
            if (regression && disable_bw_h2)
                continue;

            cout << "Running " << (regression ? "backward" : "forward")
                 << " mutex detection and operator pruning..." << endl;
            int mutexes_detected;
            try {
                mutexes_detected = h2.compute(
                    variables, operators, axioms, initial_state, goals, mutexes,
                    regression);
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

            cout << "  Detecting unreachable fluents..." << endl;
            int unreachable_result;
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
            bool unreachable_detected = unreachable_result != 0;
            cout << "  Unreachable fluents found: " << unreachable_result
                 << endl;

            cout << "  Removing spurious operators..." << endl;
            bool spurious_detected;
            try {
                spurious_detected = h2.remove_spurious_operators(operators);
            } catch (const TimeoutException &) {
                break;
            }
            cout << "  Spurious operators removed." << endl;

            update_progression |= spurious_detected || unreachable_detected ||
                                  (regression && mutexes_detected);
            update_regression |= spurious_detected || unreachable_detected ||
                                 (!regression && mutexes_detected);
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
            int count = 0;
            Atom static_fluent = Atom::no_atom;
            for (int j = 0; count < 2 && j < domain_sizes[i]; j++) {
                if (!is_unreachable(i, j)) {
                    count++;
                    static_fluent = Atom{i, j};
                }
            }
            // If there is only one possible fluent, this fluent is static.
            if (count == 1) {
                // If it was not detected as static before.
                if (!static_atoms.count(static_fluent)) {
                    static_atoms.insert(static_fluent);

                    // Set inconsistent with everything else.
                    const unordered_set<Atom> &inconsistent =
                        inconsistent_atoms[static_fluent.var]
                                          [static_fluent.value];
                    for (const auto &it : inconsistent) {
                        if (!is_unreachable(it.var, it.value)) {
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

    unreachable[var][val] = true;
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

    unreachable.resize(num_vars);
    for (int i = 0; i < num_vars; i++) {
        unreachable[i].resize(domain_sizes[i], false);
    }

    inconsistent_atoms.resize(num_vars);
    for (int i = 0; i < num_vars; i++) {
        inconsistent_atoms[i].resize(domain_sizes[i]);
        //  cout << i << "-" << num_vals[i] << endl;
    }
    // Initialize everything to NOT_REACHED (mutexes will be set to spurious).
    // Store full upper triangle including diagonal: num_atoms * (num_atoms + 1)
    // / 2 Diagonal entries represent individual atoms, off-diagonal represent
    // pairs.
    mutex_status.resize(num_atoms * (num_atoms + 1) / 2, NOT_REACHED);

    // Precompute offsets for fast pair index lookup including diagonal.
    atom_pair_offsets.reserve(num_atoms);
    unsigned current_offset = 0;
    for (unsigned atom1 = 0; atom1 < num_atoms; ++atom1) {
        // Offset needs adjustment: subtract atom1 to account for atom2
        // starting at atom1 (including diagonal)
        atom_pair_offsets.push_back(current_offset - atom1);
        // For each atom1, there are (num_atoms - atom1) entries including
        // diagonal
        current_offset += (num_atoms - atom1);
    }
    assert(atom_pair_offsets.size() == num_atoms);

    // Set to spurious variables with themselves.
    for (int var = 0; var < num_vars; ++var) {
        for (int val1 = 0; val1 < domain_sizes[var]; ++val1) {
            int atom1_id = atom_index[var][val1];
            for (int val2 = val1 + 1; val2 < domain_sizes[var]; ++val2) {
                int atom2_id = atom_index[var][val2];
                unsigned pos = get_atom_pair_id(atom1_id, atom2_id);
                mutex_status[pos] = SPURIOUS;
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
                    inconsistent_atoms[atom1.var][atom1.value].insert(atom2);
                    inconsistent_atoms[atom2.var][atom2.value].insert(atom1);

                    // Set the pairs that are mutex as spurious.
                    mutex_status[get_atom_pair_id(
                        atom_index[atom1.var][atom1.value],
                        atom_index[atom2.var][atom2.value])] = SPURIOUS;
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

bool H2Mutexes::check_initial_state_is_dead_end(
    const vector<Variable *> &variables, const State &initial_state) const {
    // Pre-compute fluent indices once
    vector<unsigned> initial_fluents;
    initial_fluents.reserve(variables.size());
    for (unsigned i = 0; i < variables.size(); i++) {
        int var = variables[i]->get_level();
        initial_fluents.push_back(atom_index[var][initial_state[variables[i]]]);
    }

    // Check with cached position calculations
    for (unsigned i = 0; i < initial_fluents.size(); i++) {
        unsigned fluent1 = initial_fluents[i];
        for (unsigned j = 0; j < initial_fluents.size(); j++) {
            if (i == j)
                continue;
            unsigned pos = get_atom_pair_id(fluent1, initial_fluents[j]);
            if (mutex_status[pos] == SPURIOUS) {
                return true;
            }
        }
    }
    return false;
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

    // The things that are mutex with the goal are not reached.
    for (const auto &[var_ptr, val] : goal) {
        int goal_var = var_ptr->get_level();
        int goal_val = val;

        const unordered_set<Atom> &goal_mutexes =
            inconsistent_atoms[goal_var][goal_val];
        for (const auto &it : goal_mutexes) {
            set_atom_not_reached(atom_index[it.var][it.value]);
        }
        for (int val1 = 0; val1 < domain_sizes[goal_var]; val1++) {
            if (val1 != goal_val) {
                set_atom_not_reached(atom_index[goal_var][val1]);
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
            operators[i], atom_index, inconsistent_atoms, regression);
    }

    if (!axioms.empty()) {
        cerr << "Error, axioms not supported by h2" << endl;
        exit(1);
    }
}

// Apply a single operator and update reachability.
// Returns true if any new pairs were marked as reachable.
bool H2Mutexes::apply_operator(unsigned op_id, vector<bool> &in_add_or_del) {
    bool updated = false;

    // Skip spurious operators
    if (h2_ops[op_id].triggered == SPURIOUS)
        return false;

    // Check if preconditions are met
    if ((h2_ops[op_id].triggered != REACHED) &&
        ((h2_ops[op_id].triggered = evaluate_atoms(h2_ops[op_id].pre)) !=
         REACHED))
        return false;

    const vector<unsigned> &op_add = h2_ops[op_id].add;
    const vector<unsigned> &op_del = h2_ops[op_id].del;
    const vector<unsigned> &op_pre = h2_ops[op_id].pre;

    // Build lookup table for O(1) membership test
    for (unsigned atom_id : op_add) {
        in_add_or_del[atom_id] = true;
    }
    for (unsigned atom_id : op_del) {
        in_add_or_del[atom_id] = true;
    }

    // First pass: update individual atoms and pairs within add effects
    for (unsigned add_i = 0; add_i < op_add.size(); add_i++) {
        unsigned p = op_add[add_i];

        // Mark individual atom as reached (diagonal entry)
        unsigned diag_p = get_atom_pair_id(p, p);
        if (mutex_status[diag_p] == NOT_REACHED) {
            mutex_status[diag_p] = REACHED;
            updated = true;
        }

        for (unsigned add_j = add_i + 1; add_j < op_add.size(); add_j++) {
            unsigned q = op_add[add_j];
            unsigned pos_pq = get_atom_pair_id(p, q);
            if (mutex_status[pos_pq] == NOT_REACHED) {
                mutex_status[pos_pq] = REACHED;
                updated = true;
            }
        }
    }

    // Second pass: check all atoms and pair with add effects
    size_t cache_base = op_id * num_atoms;
    for (unsigned atom_i = 0; atom_i < num_atoms; atom_i++) {
        if (operator_atom_cache[cache_base + atom_i])
            continue;

        // Check if individual atom is reached (diagonal entry)
        unsigned diag_atom_i = get_atom_pair_id(atom_i, atom_i);
        if (mutex_status[diag_atom_i] != REACHED)
            continue;

        if (in_add_or_del[atom_i])
            continue;

        // Check preconditions with atom_i
        bool satisfied = true;
        for (unsigned pre_i = 0; satisfied && pre_i < op_pre.size(); pre_i++) {
            unsigned pre_atom = op_pre[pre_i];
            satisfied =
                (mutex_status[get_atom_pair_id(atom_i, pre_atom)] == REACHED);
        }

        if (satisfied) {
            operator_atom_cache[cache_base + atom_i] = true;

            for (unsigned add_i = 0; add_i < op_add.size(); add_i++) {
                unsigned p = op_add[add_i];
                if (atom_i == p)
                    continue;
                unsigned pos = get_atom_pair_id(p, atom_i);
                if (mutex_status[pos] == NOT_REACHED) {
                    mutex_status[pos] = REACHED;
                    updated = true;
                }
            }
        }
    }

    // Reset lookup table
    for (unsigned atom_id : op_add) {
        in_add_or_del[atom_id] = false;
    }
    for (unsigned atom_id : op_del) {
        in_add_or_del[atom_id] = false;
    }

    return updated;
}

// Run the fixpoint computation to determine reachable atom pairs.
// Throws TimeoutException if time limit exceeded.
void H2Mutexes::run_fixpoint() {
    vector<bool> in_add_or_del(num_atoms, false);

    bool updated;
    do {
        updated = false;
        for (unsigned op_id = 0; op_id < h2_ops.size(); op_id++) {
            // Check timeout every 1000 operators for fine-grained control
            if (op_id % 1000 == 0) {
                check_timeout();
            }

            updated |= apply_operator(op_id, in_add_or_del);
        }
        // Also check after each iteration
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
            if (!is_unreachable(atom.var, atom.value)) {
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
                        inconsistent_atoms[atom1.var][atom1.value].insert(
                            atom2);
                        inconsistent_atoms[atom2.var][atom2.value].insert(
                            atom1);
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

    // Initialize operator-atom cache
    operator_atom_cache.assign(h2_ops.size() * num_atoms, false);

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

Reachability H2Mutexes::evaluate_atoms(const vector<unsigned> &atoms) {
    // Process each atom's diagonal entry and pairs sequentially in memory
    for (unsigned i = 0; i < atoms.size(); i++) {
        unsigned atom_i = atoms[i];
        // Check individual atom reachability (diagonal entry)
        unsigned diag_pos = get_atom_pair_id(atom_i, atom_i);
        if (mutex_status[diag_pos] == NOT_REACHED)
            return NOT_REACHED;
        // Check pairs with remaining atoms (cache-friendly: sequential access)
        for (unsigned j = i + 1; j < atoms.size(); j++) {
            if (mutex_status[get_atom_pair_id(atom_i, atoms[j])] == NOT_REACHED)
                return NOT_REACHED;
        }
    }
    return REACHED;
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
    const vector<vector<unordered_set<Atom>>> &inconsistent_atoms) {
    vector<bool> prepost_var(inconsistent_atoms.size(), false);

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
    // fluents mutex with prevails are e-deleted: add as negative effect
    for (unsigned j = 0; j < prevail.size(); j++) {
        int var = prevail[j].var->get_level();
        int prev = prevail[j].prev;
        if (var == -1)
            continue;

        // fluents that belong to the same variable
        for (int k = 0; k < static_cast<int>(atom_index[var].size()); k++)
            if (k != prev)
                del.push_back(atom_index[var][k]);

        // fluents mutex with the prevail
        const unordered_set<Atom> prev_mutexes = inconsistent_atoms[var][prev];
        for (const auto &it : prev_mutexes)
            del.push_back(atom_index[it.var][it.value]);
    }

    // Fluents mutex with adds are e-deleted: add as negative effect.
    for (unsigned j = 0; j < pre_post.size(); j++) {
        int var = pre_post[j].var->get_level();
        int post = pre_post[j].post;

        if (pre_post[j].is_conditional_effect) {
            continue;
        }

        if (var == -1)
            continue;

        // Fluents that belong to the same variable.
        for (int k = 0; k < static_cast<int>(atom_index[var].size()); k++) {
            if (k != post) {
                del.push_back(atom_index[var][k]);
            }
        }

        // Fluents mutex with the add.
        const unordered_set<Atom> prev_mutexes = inconsistent_atoms[var][post];
        for (const auto &it : prev_mutexes)
            del.push_back(atom_index[it.var][it.value]);
    }

    // Augmented preconditions from the disambiguation.
    const vector<Atom> &augmented = op.get_augmented_preconditions();
    for (const Atom &atom : augmented) {
        int var = atom.var;
        int val = atom.value;
        pre.push_back(atom_index[var][val]);

        if (!prepost_var[var]) {
            // Add the mutexes as deletes.
            int num_values = atom_index[var].size();
            for (int k = 0; k < num_values; k++)
                if (k != val)
                    del.push_back(atom_index[var][k]);
            const unordered_set<Atom> augmented_mutexes =
                inconsistent_atoms[var][val];
            for (const auto &it : augmented_mutexes)
                del.push_back(atom_index[it.var][it.value]);
        }
    }
}

void Op_h2::instantiate_operator_backward(
    const Operator &op, const vector<vector<unsigned>> &atom_index,
    const vector<vector<unordered_set<Atom>>> &inconsistent_atoms) {
    vector<bool> prepost_var(inconsistent_atoms.size(), false);

    const vector<Operator::Prevail> &prevail = op.get_prevail();
    for (unsigned j = 0; j < prevail.size(); j++)
        push_pre(atom_index, prevail[j].var, prevail[j].prev);

    const vector<Operator::PrePost> &pre_post = op.get_pre_post();
    for (unsigned j = 0; j < pre_post.size(); j++) {
        if (pre_post[j].pre != -1) {
            push_add(atom_index, pre_post[j].var, pre_post[j].pre);
        }

        // Revise this part. Currently backward h2 is deactivated with
        // conditional effects.
        if (!pre_post[j].is_conditional_effect) { // naive support for
                                                  // conditional effects
            push_pre(atom_index, pre_post[j].var, pre_post[j].post);
            prepost_var[pre_post[j].var->get_level()] = true;
        }

        if (pre_post[j].is_conditional_effect) { // naive support for
                                                 // conditional effects
            vector<Operator::EffCond> effect_conds = pre_post[j].effect_conds;
            for (unsigned k = 0; k < effect_conds.size(); k++)
                push_add(atom_index, effect_conds[k].var, effect_conds[k].cond);
        }
    }

    // fluents mutex with prevails are e-deleted: add as negative effect
    for (unsigned j = 0; j < prevail.size(); j++) {
        int var = prevail[j].var->get_level();
        int prev = prevail[j].prev;

        if (var == -1)
            continue;

        // fluents that belong to the same variable
        for (int k = 0; k < static_cast<int>(atom_index[var].size()); k++)
            if (k != prev)
                del.push_back(atom_index[var][k]);

        // fluents mutex with the prevail
        const unordered_set<Atom> prev_mutexes = inconsistent_atoms[var][prev];
        for (const auto &it : prev_mutexes)
            del.push_back(atom_index[it.var][it.value]);
    }

    // Fluents mutex with pres are e-deleted: add as negative effect.
    for (size_t j = 0; j < pre_post.size(); j++) {
        if (pre_post[j].is_conditional_effect)
            continue;
        // pre.push_back(atom_index[prevail[j].var][prevail[j].prev]);
        int var = pre_post[j].var->get_level();
        int pre = pre_post[j].pre;
        if (var == -1 || pre == -1)
            continue;

        // Fluents that belong to the same variable.
        int num_values = atom_index[var].size();
        for (int k = 0; k < num_values; k++)
            if (k != pre)
                del.push_back(atom_index[var][k]);

        // Fluents mutex with the add.
        const unordered_set<Atom> pre_mutexes = inconsistent_atoms[var][pre];
        for (const auto &it : pre_mutexes)
            del.push_back(atom_index[it.var][it.value]);
    }

    // Augmented preconditions from the disambiguation.
    const vector<Atom> &augmented = op.get_augmented_preconditions();
    for (const Atom &atom : augmented) {
        int var = atom.var;
        int val = atom.value;
        // Add the precondition as an add.
        if (!prepost_var[var])
            pre.push_back(atom_index[var][val]);

        // Add the mutexes as deletes.
        int num_values = atom_index[var].size();
        for (int k = 0; k < num_values; k++)
            if (k != atom.value)
                del.push_back(atom_index[var][k]);
        const unordered_set<Atom> augmented_mutexes =
            inconsistent_atoms[var][val];
        for (const auto &it : augmented_mutexes)
            del.push_back(atom_index[it.var][it.value]);
    }

    // Potential preconditions from the disambiguation.
    const vector<Atom> &potential = op.get_potential_preconditions();
    // For each variable, the set of potential deletes to add the set of
    // mutexes with ALL potential preconditions as deletes.
    unordered_map<int, unordered_set<Atom>> potential_deletes;
    for (const Atom &atom : potential) {
        int potential_var = atom.var;
        int potential_val = atom.value;

        // Add the precondition as an add.
        add.push_back(atom_index[potential_var][potential_val]);

        // Update the potential deletes.
        unordered_set<Atom> potential_deletes_aux =
            inconsistent_atoms[potential_var][potential_val]; // copy
        int num_values = atom_index[potential_var].size();
        for (int k = 0; k < num_values; k++)
            if (k != potential_val)
                potential_deletes_aux.insert(atom);

        if (potential_deletes.count(potential_var)) {
            unordered_set<Atom> intersect;
            for (const Atom &atom : potential_deletes[potential_var]) {
                if (potential_deletes_aux.contains(atom)) {
                    intersect.insert(atom);
                }
            }
            potential_deletes[potential_var].swap(intersect);
        } else {
            potential_deletes[potential_var].swap(potential_deletes_aux);
        }
    }
    for (const auto &[var, deletes] : potential_deletes) {
        for (const auto &atom : deletes) {
            del.push_back(atom_index[atom.var][atom.value]);
        }
    }
}
