#ifndef VARIABLE_H
#define VARIABLE_H

#include <iostream>
#include <string>
#include <vector>

class Variable {
    std::vector<std::string> values;
    std::string name;
    int layer;
    int level;
    std::vector<bool> reachable; // Added to prune unreachable values.
    int reachable_values;
    // Cached prefix sums for O(1) get_new_id.
    mutable std::vector<int> prefix_sum;
    mutable bool prefix_sum_valid = false;
public:
    explicit Variable(std::istream &in);
    int get_level() const;
    void set_level(int level);
    bool is_necessary() const;
    int get_range() const;
    std::string get_name() const;
    int get_layer() const {
        return layer;
    }
    bool is_derived() const {
        return layer != -1;
    }
    void generate_cpp_input(std::ofstream &outfile) const;
    void dump() const;

    const std::string &get_atom_name(int value) const {
        return values[value];
    }

    void set_unreachable(int value) {
        if (reachable[value]) {
            reachable[value] = false;
            reachable_values--;
            prefix_sum_valid = false; // Invalidate cache.
        }
    }

    bool is_reachable(int value) const {
        return reachable[value];
    }

    void remove_unreachable_atoms();
    int get_new_id(int value) const;
};

#endif
