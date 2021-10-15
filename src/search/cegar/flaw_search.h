#ifndef CEGAR_FLAW_SEARCH
#define CEGAR_FLAW_SEARCH

#include "flaw_selector.h"

#include "../open_list.h"
#include "../search_engine.h"

#include <map>

namespace successor_generator {
class SuccessorGenerator;
}

namespace cegar {
class Abstraction;
class ShortestPaths;

class FlawSearch {
    const TaskProxy task_proxy;
    std::map<int, std::vector<std::unique_ptr<Flaw>>> flaws;
    std::unique_ptr<StateOpenList> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<SearchStatistics> statistics;
    const Abstraction *abstraction;
    const ShortestPaths *shortest_paths;
    const successor_generator::SuccessorGenerator &successor_generator;

    int g_bound;

protected:
    void initialize(const Abstraction *abstraction,
                    const ShortestPaths *shortest_paths);
    SearchStatus step();
    void prune_operators(const State &state, std::vector<OperatorID> &ops) const;

    void create_applicability_flaws(
        const State &state,
        const std::vector<OperatorID> &abstract_ops,
        const std::vector<OperatorID> &concrete_ops) const;

    CartesianSet get_cartesian_set(const std::vector<int> &domain_sizes,
                                   const ConditionsProxy &conditions) const;

public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task);

    void search_for_flaws(const Abstraction *abstraction,
                          const ShortestPaths *shortest_paths);
};
}

#endif
