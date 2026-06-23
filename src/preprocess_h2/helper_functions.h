#ifndef HELPER_FUNCTIONS_H
#define HELPER_FUNCTIONS_H

#include "state.h"
#include "variable.h"

#include <ctime>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

class State;
class MutexGroup;
class Operator;
class Axiom;

double get_passed_time(clock_t start);
int get_peak_memory_in_kb();

void read_preprocessed_problem_description(
    std::istream &in, bool &metric, std::vector<Variable> &internal_variables,
    std::vector<Variable *> &variables, std::vector<MutexGroup> &mutexes,
    State &initial_state, std::vector<std::pair<Variable *, int>> &goals,
    std::vector<Operator> &operators, std::vector<Axiom> &axioms);

void dump_preprocessed_problem_description(
    const std::vector<Variable *> &variables, const State &initial_state,
    const std::vector<std::pair<Variable *, int>> &goals,
    const std::vector<Operator> &operators, const std::vector<Axiom> &axioms);

void generate_unsolvable_cpp_input(const std::string &outfile);
void generate_cpp_input(
    const std::vector<Variable *> &ordered_var, const bool &metric,
    const std::vector<MutexGroup> &mutexes, const State &initial_state,
    const std::vector<std::pair<Variable *, int>> &goals,
    const std::vector<Operator> &operators, const std::vector<Axiom> &axioms,
    const std::string &outfile);
void check_magic(std::istream &in, const std::string &magic);

namespace std {
// Hash function for vector<int> to enable use in unordered containers
template<>
struct hash<vector<int>> {
    size_t operator()(const vector<int> &vec) const noexcept {
        size_t seed = vec.size();
        for (int val : vec) {
            seed ^= hash<int>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};
}

#endif
