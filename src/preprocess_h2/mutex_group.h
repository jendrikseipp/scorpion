#ifndef MUTEX_GROUP_H
#define MUTEX_GROUP_H

#include "atom.h"
#include "operator.h"
#include "state.h"

#include <iostream>
#include <unordered_set>
#include <utility>
#include <vector>

class Variable;

enum Dir {
    FW,
    BW
};
class MutexGroup {
    // Direction of the mutex.
    //  FW mutexes are not reachable from the initial state (should be pruned in
    //  BW search). BW mutexes cannot reach the goal (should be pruned in FW
    //  search). Both mutex groups contain FW and BW mutexes so they should be
    //  pruned in both directions.
    Dir direction;
    std::vector<std::pair<const Variable *, int>> facts;
public:
    MutexGroup(std::istream &in, const std::vector<Variable *> &variables);

    MutexGroup(
        const std::vector<Atom> &atoms,
        const std::vector<Variable *> &variables, bool regression);

    explicit MutexGroup(const Variable *var);
    void strip_unimportant_atoms();
    bool is_redundant() const;

    bool is_fw() const {
        return direction == FW;
    }
    int get_encoding_size() const;
    int num_facts() const {
        return static_cast<int>(facts.size());
    }
    void generate_cpp_input(std::ofstream &outfile) const;
    void dump() const;
    std::vector<Atom> get_mutex_group() const;

    void remove_unreachable_atoms();

    bool has_atom(int var, int val) const;

    const std::vector<std::pair<const Variable *, int>> &get_facts() const {
        return facts;
    }

    void add_tuples(std::unordered_set<std::vector<int>> &tuples) const;
};

extern void strip_mutexes(std::vector<MutexGroup> &mutexes);

#endif
