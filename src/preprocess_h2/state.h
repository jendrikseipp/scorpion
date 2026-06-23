#ifndef STATE_H
#define STATE_H

#include <iostream>
#include <unordered_map>
#include <vector>

class Variable;

class State {
    std::unordered_map<Variable *, int> values;
public:
    State() = default;
    State(std::istream &in, const std::vector<Variable *> &variables);

    int operator[](Variable *var) const;
    void dump() const;
    // Returns true if the state contains an unreachable atom.
    bool remove_unreachable_atoms();
};

#endif
