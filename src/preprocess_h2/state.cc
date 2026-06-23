#include "state.h"

#include "helper_functions.h"
#include "variable.h"

#include <iostream>

using namespace std;

State::State(istream &in, const vector<Variable *> &variables) {
    check_magic(in, "begin_state");
    for (Variable *var : variables) {
        int value;
        in >> value; // For axioms, this is default value.
        values[var] = value;
    }
    check_magic(in, "end_state");
}

int State::operator[](Variable *var) const {
    return values.find(var)->second;
}

void State::dump() const {
    for (const auto &value : values)
        cout << "  " << value.first->get_name() << ": " << value.second << endl;
}

bool State::remove_unreachable_atoms() {
    unordered_map<Variable *, int> new_values;
    for (auto it = values.begin(); it != values.end(); ++it) {
        Variable *var = it->first;
        int value = it->second;
        if (var->is_necessary()) {
            if (var->is_reachable(value)) {
                new_values[var] = var->get_new_id(value);
            } else {
                return true;
            }
        }
    }
    new_values.swap(values);
    return false;
}
