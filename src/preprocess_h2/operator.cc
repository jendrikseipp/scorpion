#include "operator.h"

#include "h2_mutexes.h"
#include "helper_functions.h"
#include "variable.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_set>

using namespace std;

Operator::Operator(istream &in, const vector<Variable *> &variables)
    : spurious(false) {
    check_magic(in, "begin_operator");
    in >> ws;
    getline(in, name);
    int count; // Number of prevail conditions.
    in >> count;
    prevail.reserve(count);
    for (int i = 0; i < count; i++) {
        int varNo, val;
        in >> varNo >> val;
        prevail.emplace_back(variables[varNo], val);
    }
    in >> count; // Number of pre_post conditions.
    pre_post.reserve(count);
    for (int i = 0; i < count; i++) {
        int eff_conds;
        vector<EffCond> ecs;
        in >> eff_conds;
        ecs.reserve(eff_conds);
        for (int j = 0; j < eff_conds; j++) {
            int var, value;
            in >> var >> value;
            ecs.emplace_back(variables[var], value);
        }
        int varNo, val, newVal;
        in >> varNo >> val >> newVal;
        if (eff_conds)
            pre_post.emplace_back(
                variables[varNo], std::move(ecs), val, newVal);
        else
            pre_post.emplace_back(variables[varNo], val, newVal);
    }
    in >> cost;
    check_magic(in, "end_operator");
}

void Operator::dump() const {
    cout << name << ":" << endl;
    cout << "prevail:";
    for (const auto &prev : prevail)
        cout << "  " << prev.var->get_name() << " := " << prev.prev;
    cout << endl;
    cout << "pre-post:";
    for (const auto &eff : pre_post) {
        if (eff.is_conditional_effect) {
            cout << "  if (";
            for (const auto &cond : eff.effect_conds)
                cout << cond.var->get_name() << " := " << cond.cond;
            cout << ") then";
        }
        cout << " " << eff.var->get_name() << " : " << eff.pre << " -> "
             << eff.post;
    }
    cout << endl;
}

int Operator::get_encoding_size() const {
    int size = 1 + static_cast<int>(prevail.size());
    for (const auto &eff : pre_post) {
        size += 1 + static_cast<int>(eff.effect_conds.size());
        if (eff.pre != -1)
            size += 1;
    }
    return size;
}

void Operator::strip_unimportant_effects() {
    // Remove unimportant prevail conditions
    size_t new_index = 0;
    for (size_t i = 0; i < prevail.size(); ++i) {
        if (prevail[i].var->get_level() != -1) {
            if (new_index != i)
                prevail[new_index] = std::move(prevail[i]);
            ++new_index;
        }
    }
    prevail.erase(prevail.begin() + new_index, prevail.end());

    // Remove unimportant pre_post effects
    new_index = 0;
    for (size_t i = 0; i < pre_post.size(); ++i) {
        if (pre_post[i].var->get_level() != -1) {
            // Remove effect conditions that reference removed variables
            auto &effect_conds = pre_post[i].effect_conds;
            size_t cond_new_index = 0;
            for (size_t j = 0; j < effect_conds.size(); ++j) {
                if (effect_conds[j].var->get_level() != -1) {
                    if (cond_new_index != j)
                        effect_conds[cond_new_index] =
                            std::move(effect_conds[j]);
                    ++cond_new_index;
                }
            }
            effect_conds.erase(
                effect_conds.begin() + cond_new_index, effect_conds.end());

            if (new_index != i)
                pre_post[new_index] = std::move(pre_post[i]);
            ++new_index;
        }
    }
    pre_post.erase(pre_post.begin() + new_index, pre_post.end());
}

bool Operator::is_redundant() const {
    return spurious || pre_post.empty();
}

void strip_operators(vector<Operator> &operators) {
    int old_count = static_cast<int>(operators.size());
    size_t new_index = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        operators[i].strip_unimportant_effects();
        if (!operators[i].is_redundant()) {
            if (new_index != i)
                operators[new_index] = std::move(operators[i]);
            ++new_index;
        }
    }
    operators.erase(operators.begin() + new_index, operators.end());
    cout << operators.size() << " of " << old_count << " operators necessary."
         << endl;
}

vector<int> Operator::get_signature() const {
    vector<int> sig;
    sig.push_back(cost);

    // Add sorted prevail conditions
    vector<pair<int, int>> prevail_pairs;
    prevail_pairs.reserve(prevail.size());
    for (const auto &p : prevail) {
        prevail_pairs.emplace_back(p.var->get_level(), p.prev);
    }
    sort(prevail_pairs.begin(), prevail_pairs.end());
    for (const auto &[var, val] : prevail_pairs) {
        sig.push_back(var);
        sig.push_back(val);
    }
    sig.push_back(-1); // separator

    // Add sorted pre_post conditions
    vector<vector<int>> pp_sigs;
    pp_sigs.reserve(pre_post.size());
    for (const auto &pp : pre_post) {
        vector<int> pp_sig;
        pp_sig.push_back(pp.var->get_level());
        pp_sig.push_back(pp.pre);
        pp_sig.push_back(pp.post);
        vector<pair<int, int>> ec_pairs;
        ec_pairs.reserve(pp.effect_conds.size());
        for (const auto &ec : pp.effect_conds) {
            ec_pairs.emplace_back(ec.var->get_level(), ec.cond);
        }
        sort(ec_pairs.begin(), ec_pairs.end());
        for (const auto &[var, val] : ec_pairs) {
            pp_sig.push_back(var);
            pp_sig.push_back(val);
        }
        pp_sigs.push_back(std::move(pp_sig));
    }
    sort(pp_sigs.begin(), pp_sigs.end());
    for (const auto &pp_sig : pp_sigs) {
        sig.insert(sig.end(), pp_sig.begin(), pp_sig.end());
        sig.push_back(-2); // separator between effects
    }

    return sig;
}

void remove_duplicate_operators(vector<Operator> &operators) {
    int old_count = static_cast<int>(operators.size());
    unordered_set<vector<int>> seen;
    size_t new_index = 0;
    for (size_t i = 0; i < operators.size(); ++i) {
        if (seen.insert(operators[i].get_signature()).second) {
            if (new_index != i)
                operators[new_index] = std::move(operators[i]);
            ++new_index;
        }
    }
    operators.erase(operators.begin() + new_index, operators.end());
    int removed = old_count - static_cast<int>(operators.size());
    cout << "Removed " << removed << " duplicate operators ("
         << operators.size() << " of " << old_count << " remaining)." << endl;
}

void Operator::generate_cpp_input(ofstream &outfile) const {
    outfile << "begin_operator\n";
    outfile << name << '\n';

    outfile << prevail.size() << '\n';
    for (const auto &prev : prevail) {
        assert(prev.var->get_level() != -1);
        outfile << prev.var->get_level() << " " << prev.prev << '\n';
    }

    outfile << pre_post.size() << '\n';
    for (const auto &eff : pre_post) {
        assert(eff.var->get_level() != -1);
        outfile << eff.effect_conds.size();
        for (const auto &cond : eff.effect_conds)
            outfile << " " << cond.var->get_level() << " " << cond.cond;
        outfile << " " << eff.var->get_level() << " " << eff.pre << " "
                << eff.post << '\n';
    }
    outfile << cost << '\n';
    outfile << "end_operator\n";
}

// Removes ambiguity in the preconditions,
// detects whether the operator is spurious.
void Operator::remove_ambiguity(const H2Mutexes &h2) {
    if (is_redundant())
        return;

    const int num_vars = h2.get_num_variables();

    // Use thread_local scratch buffers to avoid repeated allocation
    thread_local vector<int> preconditions;
    thread_local vector<uint8_t> original;
    thread_local vector<uint8_t> effect_var;
    thread_local vector<int> dirty_indices;

    // Ensure correct size, then full-clear the scratch state each call.
    if (static_cast<int>(preconditions.size()) != num_vars) {
        preconditions.assign(num_vars, -1);
        original.assign(num_vars, false);
        effect_var.assign(num_vars, false);
    } else {
        fill(preconditions.begin(), preconditions.end(), -1);
        fill(original.begin(), original.end(), false);
        fill(effect_var.begin(), effect_var.end(), false);
    }
    dirty_indices.clear();
    vector<Atom> effects;
    effects.reserve(pre_post.size());

    vector<Atom> known_values;
    known_values.reserve(num_vars);

    const auto mark_dirty = [&](int var) {
        if (preconditions[var] == -1 && !original[var] && !effect_var[var])
            dirty_indices.push_back(var);
    };

    for (const Prevail &prev : prevail) {
        int var = prev.var->get_level();
        if (var != -1) {
            mark_dirty(var);
            preconditions[var] = prev.prev;
            original[var] = true;
        }
    }
    for (const PrePost &effect : pre_post) {
        int var = effect.var->get_level();
        if (var != -1) {
            mark_dirty(var);
            int pre = effect.pre;
            preconditions[var] = pre;
            original[var] = (pre != -1);
            effect_var[var] = true;
            effects.emplace_back(var, effect.post);
        }
    }
    for (const auto &atom : augmented_preconditions) {
        mark_dirty(atom.var);
        preconditions[atom.var] = atom.value;
        original[atom.var] = true;
    }

    // Check that no precondition is unreachable or mutex with some other
    // precondition. Precompute atom indices for efficiency.
    vector<pair<int, unsigned>> precond_with_indices;
    precond_with_indices.reserve(num_vars);
    for (int i = 0; i < num_vars; i++) {
        if (preconditions[i] != -1) {
            unsigned atom_id = h2.get_atom_id(i, preconditions[i]);
            if (h2.is_unreachable_by_id(atom_id)) {
                spurious = true;
                return;
            }
            precond_with_indices.emplace_back(i, atom_id);
        }
    }

    // Check pairwise mutex using precomputed indices.
    // precond_with_indices is built in increasing variable order, and atom ids
    // are assigned by variable order too, so the stored atom ids are ordered.
    for (size_t i = 0; i < precond_with_indices.size(); i++) {
        unsigned atom_i = precond_with_indices[i].second;
        for (size_t j = i + 1; j < precond_with_indices.size(); j++) {
            if (h2.are_mutex_by_ordered_index(
                    atom_i, precond_with_indices[j].second)) {
                spurious = true;
                return;
            }
        }
    }

    // Build "blocked" bitset: an atom is blocked if it's mutex with ANY known
    // precondition. This replaces O(|known| * |domain|) mutex_status lookups
    // with bitset ops.
    int total_atoms = h2.get_num_atoms();

    // Thread-local blocked bitsets to avoid allocation overhead
    // Thread-local blocked bitsets to avoid allocation overhead.
    thread_local vector<uint8_t>
        blocked; // mutex with any known precond or same-var
    thread_local vector<uint8_t>
        eff_blocked; // mutex with any effect atom or same-var
    // Clear the blocked bitsets (full clear each call).
    if (static_cast<int>(blocked.size()) < total_atoms) {
        blocked.assign(total_atoms, false);
        eff_blocked.assign(total_atoms, false);
    } else {
        fill(blocked.begin(), blocked.begin() + total_atoms, false);
        fill(eff_blocked.begin(), eff_blocked.begin() + total_atoms, false);
    }

    // Fill blocked: the mutex lists already include same-variable alternatives.
    for (const auto &[kvar, atom_idx] : precond_with_indices) {
        int kval = preconditions[kvar];
        for (unsigned m : h2.get_mutex_indices(kvar, kval))
            blocked[m] = true;
    }

    // Fill eff_blocked: effect mutex lists also include same-variable
    // alternatives.
    for (const Atom &eff : effects) {
        for (unsigned m : h2.get_mutex_indices(eff.var, eff.value))
            eff_blocked[m] = true;
    }

    // blocked already contains all constraints implied by the original
    // preconditions/effects. known_values is used only for newly resolved
    // values in the iterative disambiguation loop below.
    known_values.clear();

    vector<pair<unsigned, vector<unsigned>>> candidates;
    candidates.reserve(num_vars);
    for (int i = 0; i < num_vars; i++) {
        if (preconditions[i] != -1)
            continue;

        const int num_vals = h2.get_num_values(i);
        bool check_eff = !effect_var[i];
        int num_unreachable = h2.get_num_unreachable_values(i);

        // Count and collect surviving values using bitset checks.
        int reachable_count = 0;
        vector<unsigned> surviving_values;
        surviving_values.reserve(num_vals);
        if (num_unreachable == 0) {
            reachable_count = num_vals;
            for (int j = 0; j < num_vals; j++) {
                unsigned atom_ij = h2.get_atom_id(i, j);
                if (!blocked[atom_ij] && !(check_eff && eff_blocked[atom_ij]))
                    surviving_values.push_back(j);
            }
        } else {
            for (int j = 0; j < num_vals; j++) {
                unsigned atom_ij = h2.get_atom_id(i, j);
                if (h2.is_unreachable_by_id(atom_ij))
                    continue;
                reachable_count++;
                if (!blocked[atom_ij] && !(check_eff && eff_blocked[atom_ij]))
                    surviving_values.push_back(j);
            }
        }

        if (reachable_count == 0) {
            spurious = true;
            return;
        }
        if (surviving_values.empty()) {
            spurious = true;
            return;
        }
        if (surviving_values.size() == static_cast<size_t>(reachable_count) &&
            surviving_values.size() > 1) {
            // No values eliminated — skip
            continue;
        }
        if (surviving_values.size() == 1) {
            // Resolve directly
            mark_dirty(i);
            preconditions[i] = surviving_values[0];
            known_values.emplace_back(i, surviving_values[0]);
            continue;
        }

        // Multiple surviving values — retain only the current survivors.
        candidates.emplace_back(i, std::move(surviving_values));
    }

    // Disambiguation loop: extend blocked set with newly resolved atoms
    while (!known_values.empty()) {
        // Build additional blocked atoms from newly known values
        vector<unsigned> new_atoms;
        for (const Atom &atom : known_values) {
            int kvar = atom.var, kval = atom.value;
            for (unsigned m : h2.get_mutex_indices(kvar, kval)) {
                if (!blocked[m]) {
                    blocked[m] = true;
                    new_atoms.push_back(m);
                }
            }
        }

        if (new_atoms.empty())
            break; // No new constraints

        vector<Atom> aux_values;
        aux_values.reserve(candidates.size());
        for (size_t cand_idx = 0; cand_idx < candidates.size();) {
            auto &[var, candidate_var] = candidates[cand_idx];
            bool check_eff = !effect_var[var];

            size_t write_idx = 0;
            for (size_t ri = 0; ri < candidate_var.size(); ri++) {
                int val = candidate_var[ri];
                unsigned atom_v = h2.get_atom_id(var, val);
                if (!blocked[atom_v] && !(check_eff && eff_blocked[atom_v]))
                    candidate_var[write_idx++] = val;
            }
            candidate_var.resize(write_idx);

            if (candidate_var.empty()) {
                spurious = true;
                return;
            } else if (candidate_var.size() == 1) {
                int new_val = candidate_var[0];
                aux_values.emplace_back(var, new_val);
                mark_dirty(var);
                preconditions[var] = new_val;
                candidates[cand_idx] = std::move(candidates.back());
                candidates.pop_back();
            } else {
                ++cand_idx;
            }
        }
        known_values.swap(aux_values);
    }

    // New preconditions are added.
    for (int i : dirty_indices)
        if (preconditions[i] != -1 && !original[i])
            augmented_preconditions.push_back(Atom{i, preconditions[i]});

    // Potential preconditions are set (important for backwards h^2)
    // Note: they may overlap with augmented preconditions.
    // The exact upper bound is monotone nonincreasing across repeated
    // disambiguation calls because augmented preconditions only accumulate, so
    // once capacity has been grown on the first call we can reuse it.
    if (potential_preconditions.capacity() == 0) {
        size_t potential_preconditions_upper_bound = 0;
        for (const PrePost &effect : pre_post) {
            if (effect.pre != -1)
                continue;
            int var = effect.var->get_level();
            potential_preconditions_upper_bound +=
                (preconditions[var] != -1) ? 1 : h2.get_num_values(var);
        }
        potential_preconditions.reserve(potential_preconditions_upper_bound);
    }
    potential_preconditions.clear();
    for (const PrePost &effect : pre_post) {
        // For each undefined precondition.
        if (effect.pre != -1)
            continue;

        int var = effect.var->get_level();
        if (preconditions[var] != -1) {
            potential_preconditions.emplace_back(var, preconditions[var]);
            continue;
        }

        // For each fluent, check conflicts using the blocked bitset (O(1) per
        // value)
        for (int val = 0; val < h2.get_num_values(var); val++) {
            unsigned atom_v = h2.get_atom_id(var, val);
            if (!blocked[atom_v])
                potential_preconditions.emplace_back(var, val);
        }
    }
}

void Operator::remove_unreachable_atoms(const vector<Variable *> &variables) {
    vector<Prevail> newprev;
    newprev.reserve(prevail.size());
    for (Prevail &prev : prevail) {
        if (prev.var->is_necessary()) {
            prev.remove_unreachable_atoms();
            newprev.push_back(prev);
        }
    }
    prevail = std::move(newprev);
    for (PrePost &effect : pre_post) {
        effect.remove_unreachable_atoms();
    }
    augmented_preconditions_var.reserve(augmented_preconditions.size());
    for (const auto &[var, value] : augmented_preconditions) {
        if (variables[var]->is_necessary()) {
            augmented_preconditions_var.emplace_back(
                variables[var], variables[var]->get_new_id(value));
        }
    }
    potential_preconditions_var.reserve(potential_preconditions.size());
    for (const auto &[var, value] : potential_preconditions) {
        if (variables[var]->is_necessary()) {
            potential_preconditions_var.emplace_back(
                variables[var], variables[var]->get_new_id(value));
        }
    }
}

void Operator::include_augmented_preconditions() {
    for (const auto &[var, val] : augmented_preconditions_var) {
        bool included_in_eff = false;
        for (auto it = pre_post.begin(); it != pre_post.end();) {
            if (it->var->get_level() == var->get_level()) {
                if (it->pre != -1) {
                    cerr << "Assertion error: augmented precondition was "
                            "already encoded in the operator\n"
                         << name << endl;
                    exit(1);
                }
                if (it->post != val) {
                    it->pre = val;
                    included_in_eff = true;
                    ++it;
                } else {
                    // Remove prepost
                    it = pre_post.erase(it);
                }
                break;
            } else {
                ++it;
            }
        }
        if (!included_in_eff) {
            prevail.emplace_back(var, val);
        }
    }
    augmented_preconditions.clear();
    augmented_preconditions.shrink_to_fit();
}

int Operator::count_potential_noeff_preconditions() const {
    unordered_set<Variable *> found, found_eff;
    for (const auto &[var, val] : potential_preconditions_var) {
        bool isaug = false;
        for (const auto &[aug_var, aug_val] : augmented_preconditions_var) {
            if (aug_var->get_level() == var->get_level()) {
                isaug = true;
                break;
            }
        }
        if (isaug)
            continue;
        for (const auto &effect : pre_post) {
            if (effect.var->get_level() == var->get_level()) {
                if (effect.post == val) {
                    found_eff.insert(var);
                } else {
                    found.insert(var);
                }
                break;
            }
        }
    }
    for (Variable *var : found_eff) {
        found.erase(var);
    }

    return found.size();
}

int Operator::count_potential_preconditions() const {
    unordered_set<Variable *> found;
    for (const auto &[var, val] : potential_preconditions_var) {
        bool isaug = false;
        for (const auto &[aug_var, aug_val] : augmented_preconditions_var) {
            if (aug_var->get_level() == var->get_level()) {
                isaug = true;
                break;
            }
        }
        if (isaug)
            continue;

        found.insert(var);
    }

    return found.size();
}
