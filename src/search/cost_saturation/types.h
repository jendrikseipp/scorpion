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

// Map concrete States to abstract state IDs.
using StateMap = std::function<int (const State &)>;
using AbstractionAndStateMap = std::pair<std::unique_ptr<Abstraction>, StateMap>;

using CostPartitioning = std::vector<std::vector<int>>;
using CostPartitionings = std::vector<CostPartitioning>;
}

#endif
