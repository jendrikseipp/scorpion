#ifndef COST_SATURATION_DIVERSIFIER_H
#define COST_SATURATION_DIVERSIFIER_H

#include "types.h"

class State;
class TaskProxy;

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class CostPartitionedHeuristic;

class Diversifier {
    std::vector<int> portfolio_h_values;
    std::vector<std::vector<int>> local_state_ids_by_sample;

public:
    Diversifier(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        std::function<int (const State &state)> sampling_heuristic,
        int num_samples,
        const std::shared_ptr<utils::RandomNumberGenerator> &rng);

    bool is_diverse(const CostPartitionedHeuristic &cp);
};
}

#endif
