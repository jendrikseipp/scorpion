#ifndef CEGAR_TYPES_H
#define CEGAR_TYPES_H

#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

struct FactPair;

namespace cegar {
class Abstraction;
class AbstractState;
class CartesianSet;
struct Transition;

enum class HUpdateStrategy {
    STATES_ON_TRACE,
    DIJKSTRA_FROM_UNCONNECTED_ORPHANS,
};

using AbstractStates = std::vector<std::unique_ptr<AbstractState>>;
using CartesianSets = std::vector<std::unique_ptr<CartesianSet>>;
using Facts = std::vector<FactPair>;
// TODO: Store goals IDs in vector once we no longer use A* search.
using Goals = std::unordered_set<int>;
using Loops = std::vector<int>;
using NodeID = int;
using Operators = std::vector<int>;
using Solution = std::deque<Transition>;
using Transitions = std::vector<Transition>;

const int UNDEFINED = -1;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();
}

#endif
