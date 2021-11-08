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

bool compare_flaws(std::shared_ptr<Flaw> &a, std::shared_ptr<Flaw> &b);

class FlawSearch {
    const TaskProxy task_proxy;
    // sorted by goal distance!
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

    mutable std::priority_queue<std::shared_ptr<Flaw>, std::vector<std::shared_ptr<Flaw>>, decltype( &compare_flaws)> flaws;
    mutable std::set<int> refined_abstract_states;
    mutable size_t num_searches;
    mutable size_t num_overall_found_flaws;
    mutable size_t num_overall_refined_flaws;
    mutable size_t num_overall_expanded_concrete_states;
    mutable int min_flaw_h_value;
    mutable std::unordered_map<int, int> concrete_state_to_abstract_state;

protected:
    void initialize(const std::vector<int> *domain_sizes,
                    const Abstraction *abstraction,
                    const ShortestPaths *shortest_paths);

    SearchStatus step();

    int get_abstract_state_id(const State &state) const;

    void generate_abstraction_operators(
        const State &state,
        std::vector<OperatorID> &abstraction_ops,
        std::vector<Transition> &abstraction_trs) const;

    void prune_operators(
        const std::vector<OperatorID> &applicable_ops,
        const std::vector<OperatorID> &abstraction_ops,
        const std::vector<Transition> &abstraction_trs,
        std::vector<OperatorID> &valid_ops,
        std::vector<Transition> &valid_trs,
        std::vector<Transition> &invalid_trs) const;

    void create_applicability_flaws(
        const State &state,
        const std::vector<Transition> &valid_trs) const;

    bool create_deviation_flaws(
        const State &state,
        const State &next_state,
        const OperatorID &op_id,
        const std::vector<Transition> &invalid_trs) const;

    Solution get_abstract_solution(
        const State &concrete_state,
        const AbstractState &flawed_abstract_state,
        const Transition &flawed_tr) const;

    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;

public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task, bool debug);

    void get_flaws(std::map<int, std::vector<Flaw>> &flaw_map);

    std::unique_ptr<Flaw> search_for_flaws(const std::vector<int> *domain_sizes,
                                           const Abstraction *abstraction,
                                           const ShortestPaths *shortest_paths);

    std::unique_ptr<Flaw> get_next_flaw(const std::vector<int> *domain_sizes,
                                        const Abstraction *abstraction,
                                        const ShortestPaths *shortest_paths);

    void print_statistics() const;
};
}

#endif
