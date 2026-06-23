#include "variable.h"

#include "helper_functions.h"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace std;

Variable::Variable(istream &in) : layer(-1), level(-1) {
    int range;
    check_magic(in, "begin_variable");
    in >> ws >> name >> layer >> range >> ws;
    values.reserve(range);
    for (int i = 0; i < range; ++i) {
        string line;
        getline(in, line);
        values.push_back(std::move(line));
    }
    check_magic(in, "end_variable");
    reachable_values = range;
    reachable.assign(range, true);
}

void Variable::set_level(int theLevel) {
    level = theLevel;
}

int Variable::get_level() const {
    return level;
}

int Variable::get_range() const {
    return static_cast<int>(values.size());
}

string Variable::get_name() const {
    return name;
}

bool Variable::is_necessary() const {
    return level != -1 && reachable_values > 1;
}

void Variable::dump() const {
    cout << name << " [range " << get_range();
    if (level != -1)
        cout << "; level " << level << " values: ";
    if (is_derived())
        cout << "; derived; layer: " << layer << " values: ";
    for (const string &value : values)
        cout << " " << value;
    cout << "]" << endl;
}

void Variable::generate_cpp_input(ofstream &outfile) const {
    outfile << "begin_variable\n"
            << name << '\n'
            << layer << '\n'
            << reachable_values << '\n';
    for (size_t i = 0; i < values.size(); ++i)
        if (reachable[i])
            outfile << values[i] << '\n';
    outfile << "end_variable\n";
}

void Variable::remove_unreachable_atoms() {
    vector<string> new_values;
    new_values.reserve(reachable_values);
    for (size_t i = 0; i < values.size(); i++) {
        if (reachable[i]) {
            new_values.push_back(std::move(values[i]));
        }
    }
    values = std::move(new_values);
    reachable.assign(values.size(), true);
    prefix_sum_valid = false;
}

int Variable::get_new_id(int value) const {
    assert(reachable[value]);
    if (!reachable[value]) {
        cerr << "ERROR: Tried to update an unreachable atom" << endl;
        exit(1);
    }

    // Build prefix sum cache if not valid.
    if (!prefix_sum_valid) {
        prefix_sum.resize(reachable.size());
        int sum = 0;
        for (size_t i = 0; i < reachable.size(); ++i) {
            prefix_sum[i] = sum;
            if (reachable[i]) {
                ++sum;
            }
        }
        prefix_sum_valid = true;
    }

    return prefix_sum[value];
}
