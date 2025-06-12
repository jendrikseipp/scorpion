#ifndef COST_SATURATION_TYPES_H
#define COST_SATURATION_TYPES_H

#include "parallel_hashmap/phmap.h"

#include "../algorithms/segmented_array_pool.h"

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

using Abstractions = std::vector<std::unique_ptr<Abstraction>>;
using AbstractionFunctions = std::vector<std::unique_ptr<AbstractionFunction>>;
using AbstractionGenerators = std::vector<std::shared_ptr<AbstractionGenerator>>;
using CPFunction = std::function<CostPartitioningHeuristic(
                                     const Abstractions &, const std::vector<int> &, std::vector<int> &, const std::vector<int> &)>;
using CPHeuristics = std::vector<CostPartitioningHeuristic>;
using DeadEnds = partial_state_tree::PartialStateTree;
using Order = std::vector<int>;
using OpsPool = segmented_array_pool_template::ArrayPool<int>;
using OpsSlice = segmented_array_pool_template::ArrayPoolSlice<int>;

struct OpsSliceHash {
    std::size_t operator()(OpsSlice v) const {
        std::size_t seed = v.size();
        for (int i : v) {
            seed ^= std::hash<int>{}(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

struct OpsSliceEqualTo {
    bool operator()(OpsSlice lhs, OpsSlice rhs) const {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
};

using OpsToLabelId = phmap::flat_hash_map<OpsSlice, int, OpsSliceHash, OpsSliceEqualTo>;
using LabelIdToOps = phmap::flat_hash_map<int, OpsSlice>;
}

#endif
