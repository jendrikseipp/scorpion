#include "cost_partitioning_heuristic.h"
#include "utils.h"

#include "../utils/collections.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
int CostPartitioningHeuristic::get_lookup_table_index(int abstraction_id) const {
    for (size_t i = 0; i < lookup_tables.size(); ++i) {
        const LookupTable &table = lookup_tables[i];
        if (table.abstraction_id == abstraction_id) {
            return i;
        }
    }
    return -1;
}

void CostPartitioningHeuristic::add_h_values(
    int abstraction_id, vector<int> &&h_values) {
    if (any_of(h_values.begin(), h_values.end(), [](int h) {return h > 0;})) {
        lookup_tables.emplace_back(abstraction_id, move(h_values));
    }
}

void CostPartitioningHeuristic::merge_h_values(
    int abstraction_id, vector<int> &&h_values) {
    if (any_of(h_values.begin(), h_values.end(), [](int h) {return h > 0 && h != INF;})) {
        int lookup_table_id = get_lookup_table_index(abstraction_id);
        if (lookup_table_id == -1) {
            // There is no lookup table for this abstraction, yet.
            lookup_tables.emplace_back(abstraction_id, move(h_values));
        } else {
            // Sum values from old and new lookup table.
            vector<int> &old_h_values = lookup_tables[lookup_table_id].h_values;
            assert(h_values.size() == old_h_values.size());
            for (size_t i = 0; i < h_values.size(); ++i) {
                int &h1 = old_h_values[i];
                int h2 = h_values[i];
                h1 = left_addition(h1, h2);
            }
        }
    }
}

void CostPartitioningHeuristic::add(CostPartitioningHeuristic &&other) {
    for (LookupTable &table : other.lookup_tables) {
        merge_h_values(table.abstraction_id, move(table.h_values));
    }
}

int CostPartitioningHeuristic::compute_heuristic(
    const vector<int> &abstract_state_ids) const {
    int sum_h = 0;
    for (const LookupTable &lookup_table : lookup_tables) {
        assert(utils::in_bounds(lookup_table.abstraction_id, abstract_state_ids));
        int state_id = abstract_state_ids[lookup_table.abstraction_id];
        assert(utils::in_bounds(state_id, lookup_table.h_values));
        int h = lookup_table.h_values[state_id];
        assert(h >= 0);
        if (h == INF) {
            return INF;
        }
        sum_h += h;
        assert(sum_h >= 0);
    }
    return sum_h;
}

int CostPartitioningHeuristic::get_num_lookup_tables() const {
    return lookup_tables.size();
}

int CostPartitioningHeuristic::get_num_heuristic_values() const {
    int num_values = 0;
    for (const auto &lookup_table : lookup_tables) {
        num_values += lookup_table.h_values.size();
    }
    return num_values;
}

int CostPartitioningHeuristic::estimate_size_in_kb() const {
    return (get_num_heuristic_values() * sizeof(int) +
            lookup_tables.size() * sizeof(vector<int>)) / 1024.;
}

void CostPartitioningHeuristic::mark_useful_abstractions(
    vector<bool> &useful_abstractions) const {
    for (const auto &lookup_table : lookup_tables) {
        assert(utils::in_bounds(lookup_table.abstraction_id, useful_abstractions));
        useful_abstractions[lookup_table.abstraction_id] = true;
    }
}
}
