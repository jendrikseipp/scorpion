#ifndef CARTESIAN_ABSTRACTIONS_TYPES_H
#define CARTESIAN_ABSTRACTIONS_TYPES_H

#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

#include <parallel_hashmap/phmap.h>

struct FactPair;

namespace cartesian_abstractions {
class AbstractState;
class CartesianSet;
struct Transition;

enum class DotGraphVerbosity {
    SILENT,
    WRITE_TO_CONSOLE,
    WRITE_TO_FILE
};

enum class TransitionRepresentation {
    STORE,
    COMPUTE,
};

enum class MatcherVariable : char {
    UNAFFECTED,
    SINGLE_VALUE,
    FULL_DOMAIN,
};
static_assert(sizeof(MatcherVariable) == 1, "MatcherVariable has unexpected size");

using AbstractStates = std::deque<std::unique_ptr<AbstractState>>;
using CartesianSets = std::vector<std::unique_ptr<CartesianSet>>;
using Cost = uint64_t;
using Facts = std::vector<FactPair>;
using Goals = std::unordered_set<int>;
using Loops = std::vector<int>;
using Matcher = std::vector<MatcherVariable>;
using NodeID = int;
using Operators = std::vector<int>;
using OptimalTransitions = phmap::flat_hash_map<int, std::vector<int>>;
using Solution = std::deque<Transition>;
using Transitions = std::vector<Transition>;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();
const int UNDEFINED = -1;
const Cost INF_COSTS = std::numeric_limits<Cost>::max();
const Cost UNDEFINED_COST = std::numeric_limits<Cost>::max() - 1;
}

#endif
