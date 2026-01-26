#include "axiom.h"

#include "helper_functions.h"
#include "variable.h"

#include <cassert>
#include <fstream>
#include <iostream>
using namespace std;

Axiom::Axiom(istream &in, const vector<Variable *> &variables) {
    check_magic(in, "begin_rule");
    int count; // number of conditions
    in >> count;
    conditions.reserve(count);
    for (int i = 0; i < count; i++) {
        int varNo, val;
        in >> varNo >> val;
        conditions.emplace_back(variables[varNo], val);
    }
    int varNo, oldVal, newVal;
    in >> varNo >> oldVal >> newVal;
    effect_var = variables[varNo];
    old_val = oldVal;
    effect_val = newVal;
    check_magic(in, "end_rule");
}

bool Axiom::is_redundant() const {
    return effect_var->get_level() == -1;
}

void strip_axioms(vector<Axiom> &axioms) {
    int old_count = static_cast<int>(axioms.size());
    size_t new_index = 0;
    for (size_t i = 0; i < axioms.size(); ++i) {
        if (!axioms[i].is_redundant()) {
            if (new_index != i)
                axioms[new_index] = std::move(axioms[i]);
            ++new_index;
        }
    }
    axioms.erase(axioms.begin() + new_index, axioms.end());
    cout << axioms.size() << " of " << old_count << " axiom rules necessary."
         << endl;
}

void Axiom::dump() const {
    cout << "axiom:" << endl;
    cout << "conditions:";
    for (const Condition &condition : conditions)
        cout << "  " << condition.var->get_name() << " := " << condition.cond;
    cout << endl;
    cout << "derived:" << endl;
    cout << effect_var->get_name() << " -> " << effect_val << endl;
    cout << endl;
}

int Axiom::get_encoding_size() const {
    return 1 + static_cast<int>(conditions.size());
}

void Axiom::generate_cpp_input(ofstream &outfile) const {
    assert(effect_var->get_level() != -1);
    outfile << "begin_rule" << endl;
    outfile << conditions.size() << endl;
    for (const Condition &condition : conditions) {
        assert(condition.var->get_level() != -1);
        outfile << condition.var->get_level() << " " << condition.cond << endl;
    }
    outfile << effect_var->get_level() << " " << old_val << " " << effect_val
            << endl;
    outfile << "end_rule" << endl;
}
