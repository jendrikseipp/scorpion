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

enum class DotGraphVerbosity {
    SILENT,
    WRITE_TO_CONSOLE,
    WRITE_TO_FILE
};

using AbstractStates = std::vector<std::unique_ptr<AbstractState>>;
using Cost = uint64_t;
using Goals = std::unordered_set<int>;
using NodeID = int;
using Loops = std::vector<int>;
using Solution = std::deque<Transition>;
using Transitions = std::vector<Transition>;

const int UNDEFINED = -1;

// Positive infinity. The name "INFINITY" is taken by an ISO C99 macro.
const int INF = std::numeric_limits<int>::max();
const Cost INF_COSTS = std::numeric_limits<Cost>::max();
}

#endif
