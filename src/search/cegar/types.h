#ifndef CEGAR_TYPES_H
#define CEGAR_TYPES_H

#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

namespace cegar {
class AbstractState;
struct Transition;

enum class HUpdateStrategy {
    STATES_ON_TRACE,
    DIJKSTRA_FROM_UNCONNECTED_ORPHANS,
};

using AbstractStates = std::vector<std::unique_ptr<AbstractState>>;
// TODO: Store goals IDs in vector once we no longer use A* search.
using Goals = std::unordered_set<int>;
using NodeID = int;
using Loops = std::vector<int>;
using Solution = std::deque<Transition>;
using Transitions = std::vector<Transition>;

const int UNDEFINED = -1;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();
}

#endif
