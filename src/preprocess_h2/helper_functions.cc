#include "helper_functions.h"

#include "axiom.h"
#include "mutex_group.h"
#include "operator.h"
#include "state.h"
#include "variable.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace std;

static const int SAS_FILE_VERSION = 3;
static const int PRE_FILE_VERSION = SAS_FILE_VERSION;

double get_passed_time(clock_t start) {
    return static_cast<double>(clock() - start) / CLOCKS_PER_SEC;
}

int get_peak_memory_in_kb() {
    // Returns peak memory usage in KB, or -1 on error
    int memory_in_kb = -1;

#ifdef __linux__
    ifstream procfile("/proc/self/status");
    string word;
    while (procfile.good()) {
        procfile >> word;
        if (word == "VmPeak:") {
            procfile >> memory_in_kb;
            break;
        }
        // Skip to end of line.
        procfile.ignore(numeric_limits<streamsize>::max(), '\n');
    }
    if (procfile.fail())
        memory_in_kb = -1;
#endif

    return memory_in_kb;
}

void check_magic(istream &in, const string &magic) {
    string word;
    in >> word;
    if (word != magic) {
        cerr << "Failed to match magic word '" << magic << "'." << endl;
        cerr << "Got '" << word << "'." << endl;
        if (magic == "begin_version") {
            cerr << "Possible cause: you are running the preprocessor "
                 << "on a translator file from an " << endl
                 << "older version." << endl;
        }
        exit(1);
    }
}

void read_and_verify_version(istream &in) {
    int version;
    check_magic(in, "begin_version");
    in >> version;
    check_magic(in, "end_version");
    if (version != SAS_FILE_VERSION) {
        cerr << "Expected translator file version " << SAS_FILE_VERSION
             << ", got " << version << "." << endl;
        cerr << "Exiting." << endl;
        exit(1);
    }
}

void read_metric(istream &in, bool &metric) {
    check_magic(in, "begin_metric");
    in >> metric;
    check_magic(in, "end_metric");
}

void read_variables(
    istream &in, vector<Variable> &internal_variables,
    vector<Variable *> &variables) {
    int count;
    in >> count;
    // Important so that the iterators stored in variables are valid.
    internal_variables.reserve(count);
    for (int i = 0; i < count; i++) {
        internal_variables.push_back(Variable(in));
        variables.push_back(&internal_variables.back());
    }
}

void read_mutexes(
    istream &in, vector<MutexGroup> &mutexes,
    const vector<Variable *> &variables) {
    size_t count;
    in >> count;
    for (size_t i = 0; i < count; ++i)
        mutexes.push_back(MutexGroup(in, variables));
}

void read_goal(
    istream &in, const vector<Variable *> &variables,
    vector<pair<Variable *, int>> &goals) {
    check_magic(in, "begin_goal");
    int count;
    in >> count;
    for (int i = 0; i < count; i++) {
        int varNo, val;
        in >> varNo >> val;
        goals.push_back(make_pair(variables[varNo], val));
    }
    check_magic(in, "end_goal");
}

void dump_goal(const vector<pair<Variable *, int>> &goals) {
    cout << "Goal Conditions:" << endl;
    for (const auto &[var, val] : goals)
        cout << "  " << var->get_name() << ": " << val << endl;
}

void read_operators(
    istream &in, const vector<Variable *> &variables,
    vector<Operator> &operators) {
    int count;
    in >> count;
    for (int i = 0; i < count; i++)
        operators.push_back(Operator(in, variables));
}

void read_axioms(
    istream &in, const vector<Variable *> &variables, vector<Axiom> &axioms) {
    int count;
    in >> count;
    for (int i = 0; i < count; i++)
        axioms.push_back(Axiom(in, variables));
}

void read_preprocessed_problem_description(
    istream &in, bool &metric, vector<Variable> &internal_variables,
    vector<Variable *> &variables, vector<MutexGroup> &mutexes,
    State &initial_state, vector<pair<Variable *, int>> &goals,
    vector<Operator> &operators, vector<Axiom> &axioms) {
    read_and_verify_version(in);
    read_metric(in, metric);
    read_variables(in, internal_variables, variables);
    read_mutexes(in, mutexes, variables);
    initial_state = State(in, variables);
    read_goal(in, variables, goals);
    read_operators(in, variables, operators);
    read_axioms(in, variables, axioms);
}

void dump_preprocessed_problem_description(
    const vector<Variable *> &variables, const State &initial_state,
    const vector<pair<Variable *, int>> &goals,
    const vector<Operator> &operators, const vector<Axiom> &axioms) {
    cout << "Variables (" << variables.size() << "):" << endl;
    for (Variable *var : variables)
        var->dump();

    cout << "Initial State:" << endl;
    initial_state.dump();
    dump_goal(goals);

    for (const Operator &op : operators)
        op.dump();
    for (const Axiom &axiom : axioms)
        axiom.dump();
}

void generate_cpp_input(
    const vector<Variable *> &ordered_vars, const bool &metric,
    const vector<MutexGroup> &mutexes, const State &initial_state,
    const vector<pair<Variable *, int>> &goals,
    const vector<Operator> &operators, const vector<Axiom> &axioms,
    const string &outfile) {
    ofstream out;
    out.open(outfile, ios::out);

    out << "begin_version\n";
    out << PRE_FILE_VERSION << '\n';
    out << "end_version\n";

    out << "begin_metric\n";
    out << metric << '\n';
    out << "end_metric\n";

    int num_vars = ordered_vars.size();
    out << num_vars << '\n';
    for (Variable *var : ordered_vars)
        var->generate_cpp_input(out);

    out << mutexes.size() << '\n';
    for (const MutexGroup &mutex : mutexes)
        mutex.generate_cpp_input(out);

    out << "begin_state\n";
    for (Variable *var : ordered_vars)
        out << initial_state[var] << '\n'; // for axioms default value
    out << "end_state\n";

    vector<int> ordered_goal_values;
    ordered_goal_values.resize(num_vars, -1);
    for (const auto &[var, val] : goals) {
        int var_index = var->get_level();
        assert(var_index >= 0 && var_index < num_vars);
        ordered_goal_values[var_index] = val;
    }
    out << "begin_goal\n";
    out << goals.size() << '\n';
    for (int i = 0; i < num_vars; i++)
        if (ordered_goal_values[i] != -1)
            out << i << " " << ordered_goal_values[i] << '\n';
    out << "end_goal\n";

    out << operators.size() << '\n';
    for (const Operator &op : operators)
        op.generate_cpp_input(out);

    out << axioms.size() << '\n';
    for (const Axiom &axiom : axioms)
        axiom.generate_cpp_input(out);

    out.close();
}
void generate_unsolvable_cpp_input(const string &outfile) {
    cout << "Unsolvable task in preprocessor" << endl;
    ofstream out;
    out.open(outfile, ios::out);
    out << "begin_version\n";
    out << PRE_FILE_VERSION << '\n';
    out << "end_version\n";

    out << "begin_metric\n1\nend_metric\n";

    // variables
    out << "1\n"
        << "begin_variable\n"
        << "var0\n"
        << "-1\n"
        << "2\n"
        << "Atom dummy(val1)\n"
        << "Atom dummy(val2)\n"
        << "end_variable\n";

    // Mutexes
    out << "0\n";

    // Initial state and goal
    out << "begin_state\n0\nend_state\n";
    out << "begin_goal\n"
        << "1\n"
        << "0 1\n"
        << "end_goal\n";

    // Operators
    out << "0\n";

    // Axioms
    out << "0\n";

    out.close();
}
