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
    mutable std::map<int, std::vector<Flaw>> applicability_flaws;
    mutable std::map<int, std::vector<Flaw>> deviation_flaws;
    std::unique_ptr<StateOpenList> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<SearchStatistics> statistics;
    const Abstraction *abstraction;
    const ShortestPaths *shortest_paths;
    const std::vector<int> *domain_sizes;
    const successor_generator::SuccessorGenerator &successor_generator;

    int g_bound;
    int f_bound;
    bool debug;

protected:
    void initialize(const std::vector<int> *domain_sizes,
                    const Abstraction *abstraction,
                    const ShortestPaths *shortest_paths);

    SearchStatus step();

    void generate_abstraction_operators(
        const State &state,
        std::vector<OperatorID> &abstraction_ops) const;

    void prune_operators(
        const std::vector<OperatorID> &applicable_ops,
        const std::vector<OperatorID> &abstraction_ops,
        std::vector<OperatorID> &valid_ops) const;

    void create_applicability_flaws(
        const State &state,
        const std::vector<OperatorID> &abstraction_ops,
        const std::vector<OperatorID> &valid_ops) const;

    bool create_deviation_flaws(
        const State &state,
        const State &next_state,
        const OperatorID op_id) const;

    Solution get_abstract_solution(
        const State &concrete_state,
        const AbstractState &flawed_abstract_state,
        const Transition &flawed_tr) const;

    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;

public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task, bool debug);

    std::unique_ptr<Flaw> search_for_flaws(const std::vector<int> *domain_sizes,
                                           const Abstraction *abstraction,
                                           const ShortestPaths *shortest_paths);
};
}

#endif
