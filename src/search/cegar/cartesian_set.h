#ifndef CEGAR_CARTESIAN_SET_H
#define CEGAR_CARTESIAN_SET_H

#include "../per_state_bitset.h"

#include <ostream>
#include <vector>

namespace cegar {
struct VariableInfo {
    int domain_size;
    int block_index;

    VariableInfo(int domain_size, int block_index)
        : domain_size(domain_size), block_index(block_index) {
    }

    int get_num_blocks() const {
        return BitsetMath::compute_num_blocks(domain_size);
    }
};


/*
  For each variable store a subset of its domain.

  The underlying data structure is a vector of bitsets.
*/
class CartesianSet {
    std::vector<BitsetMath::Block> domains;

    static std::vector<VariableInfo> var_infos;
    static int total_num_blocks;

    BitsetView get_view(int var);
    ConstBitsetView get_view(int var) const;

public:
    explicit CartesianSet(const std::vector<int> &domain_sizes);

    static void initialize_static_members(const std::vector<int> &domain_sizes);

    void add(int var, int value);
    void set_single_value(int var, int value);
    void remove(int var, int value);
    void add_all(int var);
    void remove_all(int var);

    bool test(int var, int value) const {
        return get_view(var).test(value);
    }

    int count(int var) const;
    bool intersects(const CartesianSet &other, int var) const;
    bool is_superset_of(const CartesianSet &other) const;

    uint64_t estimate_size_in_bytes() const;

    int get_num_variables() const {
        return var_infos.size();
    }

    friend std::ostream &operator<<(
        std::ostream &os, const CartesianSet &cartesian_set);
};
}

#endif
