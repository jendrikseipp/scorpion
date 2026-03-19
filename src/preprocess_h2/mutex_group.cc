#include "mutex_group.h"

#include "helper_functions.h"
#include "variable.h"

#include <fstream>
#include <iostream>

using namespace std;

MutexGroup::MutexGroup(istream &in, const vector<Variable *> &variables)
    : direction(FW) {
    // Mutex groups detected in the translator are "fw" mutexes.
    int size;
    check_magic(in, "begin_mutex_group");
    in >> size;
    facts.reserve(size);
    for (int i = 0; i < size; ++i) {
        int var_no, value;
        in >> var_no >> value;
        facts.emplace_back(variables[var_no], value);
    }
    check_magic(in, "end_mutex_group");
}
MutexGroup::MutexGroup(
    const vector<Atom> &atoms_, const vector<Variable *> &variables,
    bool regression) {
    if (regression) {
        direction = BW;
    } else {
        direction = FW;
    }
    facts.reserve(atoms_.size());
    for (const auto &atom : atoms_) {
        facts.emplace_back(variables[atom.var], atom.value);
    }
}

MutexGroup::MutexGroup(const Variable *var) : direction(FW) {
    facts.reserve(var->get_range());
    for (int i = 0; i < var->get_range(); ++i) {
        facts.emplace_back(var, i);
    }
}

int MutexGroup::get_encoding_size() const {
    return static_cast<int>(facts.size());
}

void MutexGroup::dump() const {
    cout << "mutex group of size " << facts.size() << ":" << endl;
    for (const auto &[var, value] : facts) {
        cout << "   " << var->get_name() << " = " << value << " ("
             << var->get_atom_name(value) << ")" << endl;
    }
}

void MutexGroup::generate_cpp_input(ofstream &outfile) const {
    outfile << "begin_mutex_group\n" << facts.size() << '\n';
    for (const auto &[var, value] : facts) {
        outfile << var->get_level() << " " << value << '\n';
    }
    outfile << "end_mutex_group\n";
}

void MutexGroup::strip_unimportant_atoms() {
    size_t new_index = 0;
    for (size_t i = 0; i < facts.size(); ++i) {
        if (facts[i].first->get_level() != -1 &&
            facts[i].first->is_necessary()) {
            if (new_index != i)
                facts[new_index] = facts[i];
            ++new_index;
        }
    }
    facts.erase(facts.begin() + new_index, facts.end());
}

bool MutexGroup::is_redundant() const {
    // Only mutex groups that talk about two or more different
    // finite-domain variables are interesting.
    int num_facts = static_cast<int>(facts.size());
    for (int i = 1; i < num_facts; ++i)
        if (facts[i].first != facts[i - 1].first)
            return false;
    return true;
}

void strip_mutexes(vector<MutexGroup> &mutexes) {
    int old_count = static_cast<int>(mutexes.size());
    size_t new_index = 0;
    for (size_t i = 0; i < mutexes.size(); ++i) {
        mutexes[i].strip_unimportant_atoms();
        if (!mutexes[i].is_redundant()) {
            if (new_index != i)
                mutexes[new_index] = std::move(mutexes[i]);
            ++new_index;
        }
    }
    mutexes.erase(mutexes.begin() + new_index, mutexes.end());
    cout << mutexes.size() << " of " << old_count << " mutex groups necessary."
         << endl;
}

void MutexGroup::remove_unreachable_atoms() {
    vector<pair<const Variable *, int>> newfacts;
    newfacts.reserve(facts.size());
    for (const auto &[var, value] : facts) {
        if (var->is_necessary() && var->is_reachable(value)) {
            newfacts.emplace_back(var, var->get_new_id(value));
        }
    }
    facts = std::move(newfacts);
}

vector<Atom> MutexGroup::get_mutex_group() const {
    vector<Atom> invariant_group;
    invariant_group.reserve(facts.size());
    for (const auto &[var, value] : facts) {
        invariant_group.emplace_back(var->get_level(), value);
    }
    return invariant_group;
}

bool MutexGroup::has_atom(int var, int val) const {
    for (const auto &[variable, value] : facts) {
        if (variable->get_level() == var && value == val) {
            return true;
        }
    }
    return false;
}

void MutexGroup::add_tuples(unordered_set<vector<int>> &tuples) const {
    vector<Atom> atoms = get_mutex_group();
    for (size_t i = 0; i < atoms.size(); ++i) {
        int v1 = atoms[i].var;
        int a1 = atoms[i].value;

        for (size_t j = i + 1; j < atoms.size(); ++j) {
            int v2 = atoms[j].var;
            int a2 = atoms[j].value;

            if (v1 == v2)
                continue;

            if (v2 < v1) {
                tuples.emplace(vector<int>{v2, a2, v1, a1});
            } else {
                tuples.emplace(vector<int>{v1, a1, v2, a2});
            }
        }
    }
}
