#include "cartesian_set.h"

#include <sstream>

using namespace std;

namespace cegar {
BitsetView CartesianSet::get_view(int var) {
    return {
               ArrayView<BitsetMath::Block>(
                   domains.data() + var_infos[var].block_index,
                   var_infos[var].get_num_blocks()),
               var_infos[var].domain_size
    };
}

ConstBitsetView CartesianSet::get_view(int var) const {
    return {
               ConstArrayView<BitsetMath::Block>(
                   domains.data() + var_infos[var].block_index,
                   var_infos[var].get_num_blocks()),
               var_infos[var].domain_size
    };
}

bool CartesianSet::is_consistent(int var) const {
    for (int value = 0; value < var_infos[var].domain_size; ++value) {
        if (test(var, value) != get_view(var).test(value)) {
            return false;
        }
    }
    return true;
}

CartesianSet::CartesianSet(const vector<int> &domain_sizes) {
    domain_subsets.reserve(domain_sizes.size());
    for (int domain_size : domain_sizes) {
        Bitset domain(domain_size);
        domain.set();
        domain_subsets.push_back(move(domain));
    }

    var_infos.reserve(domain_sizes.size());
    int total_num_blocks = 0;
    for (int domain_size : domain_sizes) {
        int num_blocks = BitsetMath::compute_num_blocks(domain_size);
        var_infos.emplace_back(domain_size, total_num_blocks);
        total_num_blocks += num_blocks;
    }
    domains.resize(total_num_blocks, 0);
    for (size_t var = 0; var < domain_sizes.size(); ++var) {
        add_all(var);
    }
}

void CartesianSet::add(int var, int value) {
    domain_subsets[var].set(value);

    get_view(var).set(value);
    assert(is_consistent(var));
}

void CartesianSet::remove(int var, int value) {
    domain_subsets[var].reset(value);

    get_view(var).reset(value);
    assert(is_consistent(var));
}

void CartesianSet::set_single_value(int var, int value) {
    remove_all(var);
    add(var, value);
}

void CartesianSet::add_all(int var) {
    domain_subsets[var].set();

    get_view(var).set();
    assert(is_consistent(var));
}

void CartesianSet::remove_all(int var) {
    domain_subsets[var].reset();

    get_view(var).reset();
    assert(is_consistent(var));
}

void CartesianSet::set(int var, const Bitset &values) {
    domain_subsets[var] = values;

    get_view(var).reset();
    for (size_t index = 0; index < values.size(); ++index) {
        if (values.test(index)) {
            get_view(var).set(index);
        }
    }
    assert(is_consistent(var));
}

const Bitset &CartesianSet::get(int var) const {
    return domain_subsets[var];
}

int CartesianSet::count(int var) const {
    int result = domain_subsets[var].count();
    assert(is_consistent(var));
    assert(result == get_view(var).count());
    return result;
}

bool CartesianSet::intersects(const CartesianSet &other, int var) const {
    bool result = domain_subsets[var].intersects(other.domain_subsets[var]);
    assert(is_consistent(var));
    assert(result == get_view(var).intersects(other.get_view(var)));
    return result;
}

bool CartesianSet::is_superset_of(const CartesianSet &other) const {
    int num_vars = domain_subsets.size();
    for (int var = 0; var < num_vars; ++var) {
        bool is_subset = other.domain_subsets[var].is_subset_of(domain_subsets[var]);
        assert(is_subset == other.get_view(var).is_subset_of(get_view(var)));
        if (!is_subset) {
            return false;
        }
    }
    return true;
}

uint64_t CartesianSet::estimate_size_in_bytes() const {
    uint64_t bytes = 0;
    assert(domain_subsets.size() == domain_subsets.capacity());
    bytes += sizeof(domain_subsets);
    for (auto &subset : domain_subsets) {
        bytes += subset.estimate_size_in_bytes();
    }
    return bytes;
}

ostream &operator<<(ostream &os, const CartesianSet &cartesian_set) {
    int num_vars = cartesian_set.domain_subsets.size();
    string var_sep;
    os << "<";
    for (int var = 0; var < num_vars; ++var) {
        const Bitset &domain = cartesian_set.domain_subsets[var];
        vector<int> values;
        for (size_t value = 0; value < domain.size(); ++value) {
            if (domain[value])
                values.push_back(value);
        }
        assert(!values.empty());
        if (values.size() < domain.size()) {
            os << var_sep << var << "={";
            string value_sep;
            for (int value : values) {
                os << value_sep << value;
                value_sep = ",";
            }
            os << "}";
            var_sep = ",";
        }
    }
    return os << ">";
}
}
