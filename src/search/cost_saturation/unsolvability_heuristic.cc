#include "max_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"

#include "../task_utils/task_properties.h"

using namespace std;

namespace cost_saturation {
UnsolvabilityHeuristic::UnsolvabilityHeuristic(const Abstractions &abstractions) {
    for (size_t i = 0; i < abstractions.size(); ++i) {
        unsolvability_infos.emplace_back(i, vector<bool>(abstractions[i]->get_num_states(), false));
    }
}

void UnsolvabilityHeuristic::mark_unsolvable_states(int abstraction_id, const std::vector<int> &h_values) {
    for (size_t state = 0; state < h_values.size(); ++state) {
        if (h_values[state] == INF) {
            unsolvability_infos[abstraction_id].unsolvable_states[state] = true;
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
    vector<bool> &useful_abstractions) {
    // Remove bitvectors with only solvable states.
    unsolvability_infos.erase(
        remove_if(unsolvability_infos.begin(), unsolvability_infos.end(), [](const UnsolvabilityInfo &info) {
                      const vector<bool> &unsolvable = info.unsolvable_states;
                      return none_of(unsolvable.begin(), unsolvable.end(), [](bool b) {return b;});
                  }), unsolvability_infos.end());

    // Mark remaining abstractions as useful.
    for (const auto &info : unsolvability_infos) {
        useful_abstractions[info.abstraction_id] = true;
    }
}
}
