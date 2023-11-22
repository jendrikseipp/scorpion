#include "cartesian_set.h"

#include "utils.h"

#include <sstream>

using namespace std;

namespace cartesian_abstractions {
vector<VariableInfo> CartesianSet::var_infos;
int CartesianSet::total_num_blocks;

CartesianSet::CartesianSet(const vector<int> &domain_sizes) {
    domains.resize(total_num_blocks, 0);
    for (size_t var = 0; var < domain_sizes.size(); ++var) {
        add_all(var);
    }
}

void CartesianSet::set_static_members(const vector<int> &domain_sizes) {
    var_infos.clear();
    var_infos.reserve(domain_sizes.size());
    total_num_blocks = 0;
    for (int domain_size : domain_sizes) {
        int num_blocks = BitsetMath::compute_num_blocks(domain_size);
        var_infos.emplace_back(domain_size, total_num_blocks);
        total_num_blocks += num_blocks;
    }
}

void CartesianSet::add(int var, int value) {
    get_view(var).set(value);
}

void CartesianSet::remove(int var, int value) {
    get_view(var).reset(value);
}

void CartesianSet::set_single_value(int var, int value) {
    remove_all(var);
    add(var, value);
}

void CartesianSet::add_all(int var) {
    get_view(var).set();
}

void CartesianSet::remove_all(int var) {
    get_view(var).reset();
}

int CartesianSet::count(int var) const {
    return get_view(var).count();
}

vector<int> CartesianSet::get_values(int var) const {
    vector<int> values;
    int domain_size = var_infos[var].domain_size;
    for (int value = 0; value < domain_size; ++value) {
        if (test(var, value)) {
            values.push_back(value);
        }
    }
    return values;
}

bool CartesianSet::has_full_domain(int var) const {
    bool result = (count(var) == var_infos[var].domain_size);
#ifndef NDEBUG
    bool slow_result = true;
    for (int value = 0; value < var_infos[var].domain_size; ++value) {
        if (!test(var, value)) {
            slow_result = false;
            break;
        }
    }
    assert(result == slow_result);
#endif
    return result;
}

bool CartesianSet::is_superset_of(const CartesianSet &other) const {
    for (int var = 0; var < get_num_variables(); ++var) {
        bool is_subset = other.get_view(var).is_subset_of(get_view(var));
        if (!is_subset) {
            return false;
        }
    }
    return true;
}

uint64_t CartesianSet::estimate_size_in_bytes() const {
    return estimate_memory_usage_in_bytes(domains);
}

double CartesianSet::compute_size() const {
    double size = 1.0;
    for (int var = 0; var < get_num_variables(); ++var) {
        size *= count(var);
    }
    return size;
}

ostream &operator<<(ostream &os, const CartesianSet &cartesian_set) {
    string var_sep;
    os << "<";
    for (int var = 0; var < cartesian_set.get_num_variables(); ++var) {
        const ConstBitsetView &view = cartesian_set.get_view(var);
        vector<int> values;
        for (int value = 0; value < view.size(); ++value) {
            if (view.test(value))
                values.push_back(value);
        }
        assert(!values.empty());
        if (static_cast<int>(values.size()) < view.size()) {
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
