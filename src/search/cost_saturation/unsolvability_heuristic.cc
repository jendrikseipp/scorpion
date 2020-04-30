#include "max_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"

#include "../task_utils/task_properties.h"

using namespace std;

namespace cost_saturation {
UnsolvabilityHeuristic::UnsolvabilityHeuristic(
    const Abstractions &abstractions, const CPHeuristics &cp_heuristics) {
    // Ensure that we didn't mess up while moving these vectors.
    assert(!abstractions.empty());
    assert(!cp_heuristics.empty());

    int num_abstractions = abstractions.size();
    vector<vector<bool>> unsolvable;
    unsolvable.reserve(num_abstractions);
    for (int i = 0; i < num_abstractions; ++i) {
        unsolvable.emplace_back(abstractions[i]->get_num_states(), false);
    }
    vector<bool> has_unsolvable_states(num_abstractions, false);
    for (auto &cp : cp_heuristics) {
        for (const auto &lookup_table : cp.lookup_tables) {
            for (size_t state = 0; state < lookup_table.h_values.size(); ++state) {
                if (lookup_table.h_values[state] == INF) {
                    unsolvable[lookup_table.abstraction_id][state] = true;
                    has_unsolvable_states[lookup_table.abstraction_id] = true;
                }
            }
        }
    }
    for (int i = 0; i < num_abstractions; ++i) {
        if (has_unsolvable_states[i]) {
            unsolvability_infos.emplace_back(i, move(unsolvable[i]));
        }
    }
}

bool UnsolvabilityHeuristic::is_unsolvable(const vector<int> &abstract_state_ids) const {
    for (const auto &info : unsolvability_infos) {
        if (info.unsolvable_states[abstract_state_ids[info.abstraction_id]]) {
            return true;
        }
    }
    return false;
}

void UnsolvabilityHeuristic::mark_useful_abstractions(
    vector<bool> &useful_abstractions) const {
    for (const auto &info : unsolvability_infos) {
        useful_abstractions[info.abstraction_id] = true;
    }
}
}
