#ifndef COST_SATURATION_DIVERSIFIER_H
#define COST_SATURATION_DIVERSIFIER_H

#include "types.h"

class TaskProxy;

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class Diversifier {
    const int max_samples = 1000;
    std::vector<int> portfolio_h_values;
    std::vector<std::vector<int>> local_state_ids_by_sample;

public:
    Diversifier(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs,
        CPFunction cp_function,
        const std::shared_ptr<utils::RandomNumberGenerator> &rng);

    bool is_diverse(const CostPartitioning &cp);
};
}

#endif
