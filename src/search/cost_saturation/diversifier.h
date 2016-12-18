#ifndef COST_SATURATION_DIVERSIFIER_H
#define COST_SATURATION_DIVERSIFIER_H

#include "types.h"

class TaskProxy;

namespace cost_saturation {
class Diversifier {
    const int max_samples = 1000;
    std::vector<int> portfolio_h_values;
    std::vector<std::vector<int>> local_state_ids_by_sample;

public:
    Diversifier(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<int> &costs);

    bool is_diverse(const CostPartitioning &scp);

    const std::vector<std::vector<int>> &get_local_state_ids_by_sample() const;
};
}

#endif
