#ifndef COST_SATURATION_TYPES_H
#define COST_SATURATION_TYPES_H

#include <limits>
#include <memory>
#include <vector>

class State;

namespace cost_saturation {
class Abstraction;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();

using Abstractions = std::vector<std::unique_ptr<Abstraction>>;
using CostPartitioning = std::vector<std::vector<int>>;
using CostPartitionings = std::vector<CostPartitioning>;
using CPFunction = std::function<CostPartitioning(
    const Abstractions &, const std::vector<int> &, const std::vector<int> &)>;
}

#endif
