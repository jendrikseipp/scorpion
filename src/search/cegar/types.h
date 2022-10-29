#ifndef CEGAR_TYPES_H
#define CEGAR_TYPES_H

#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

struct FactPair;

namespace cegar {
class AbstractState;
class CartesianSet;
struct Transition;

enum class DotGraphVerbosity {
    SILENT,
    WRITE_TO_CONSOLE,
    WRITE_TO_FILE
};

enum class SearchStrategy {
    ASTAR,
    INCREMENTAL,
};

enum class TransitionRepresentation {
    TS,
    SG,
    TS_THEN_SG,
};

enum class Variable: char {
    UNAFFECTED,
    SINGLE_VALUE,
    FULL_DOMAIN,
};
static_assert(sizeof(Variable) == 1, "Variable has unexpected size");

using AbstractStates = std::deque<std::unique_ptr<AbstractState>>;
using CartesianSets = std::vector<std::unique_ptr<CartesianSet>>;
using Cost = uint64_t;
using Facts = std::vector<FactPair>;
// TODO: Store goals IDs in vector once we no longer use A* search.
using Goals = std::unordered_set<int>;
using Loops = std::vector<int>;
using Matcher = std::vector<Variable>;
using NodeID = int;
using Operators = std::vector<int>;
using Solution = std::deque<Transition>;
using Transitions = std::vector<Transition>;

const int UNDEFINED = -1;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();
const Cost INF_COSTS = std::numeric_limits<Cost>::max();
}

#endif
