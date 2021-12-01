#ifndef COST_SATURATION_TYPES_H
#define COST_SATURATION_TYPES_H

#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace partial_state_tree {
class PartialStateTree;
}

namespace cost_saturation {
class Abstraction;
class AbstractionFunction;
class AbstractionGenerator;
class CostPartitioningHeuristic;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();

enum class Saturator {
    ALL,
    PERIM,
    PERIMSTAR,
};

using Abstractions = std::vector<std::unique_ptr<Abstraction>>;
using AbstractionFunctions = std::vector<std::unique_ptr<AbstractionFunction>>;
using AbstractionGenerators = std::vector<std::shared_ptr<AbstractionGenerator>>;
using CPFunction = std::function<CostPartitioningHeuristic(
                                     const Abstractions &, const std::vector<int> &, std::vector<int> &, const std::vector<int> &)>;
using CPHeuristics = std::vector<CostPartitioningHeuristic>;
using DeadEnds = partial_state_tree::PartialStateTree;
using Order = std::vector<int>;
}

#endif
