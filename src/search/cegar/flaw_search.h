#ifndef CEGAR_FLAW_SEARCH_H
#define CEGAR_FLAW_SEARCH_H

#include "flaw.h"
#include "types.h"

#include "../open_list.h"
#include "../search_engine.h"
#include "../utils/timer.h"
#include "../utils/hash.h"

#include <map>
#include <queue>

namespace successor_generator {
class SuccessorGenerator;
}

namespace cegar {
class Abstraction;
class ShortestPaths;
class AbstractState;
struct Split;

class FlawSearch {
    const TaskProxy task_proxy;
    // sorted by goal distance!
    std::unique_ptr<StateOpenList> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<SearchStatistics> statistics;
    const std::vector<int> &domain_sizes;
    const Abstraction &abstraction;
    const ShortestPaths &shortest_paths;
    const successor_generator::SuccessorGenerator &successor_generator;

    int g_bound;
    int f_bound;
    bool debug;

    // Mapping from h-values to abstract_state_id to List of Flaws
    std::map<int, utils::HashMap<int, std::vector<Flaw>>> flaw_map;

    std::unordered_set<int>
    refined_abstract_states;
    size_t num_searches;
    size_t num_overall_found_flaws;
    size_t num_overall_refined_flaws;
    size_t num_overall_expanded_concrete_states;
    utils::Timer timer;

    mutable std::unordered_map<int, int> concrete_state_to_abstract_state;

protected:
    void initialize();

    SearchStatus step();

    int get_abstract_state_id(const State &state) const;

    void generate_abstraction_operators(
        const State &state,
        utils::HashSet<OperatorID> &abstraction_ops,
        utils::HashSet<Transition> &abstraction_trs) const;

    void prune_operators(
        const std::vector<OperatorID> &applicable_ops,
        const utils::HashSet<OperatorID> &abstraction_ops,
        const utils::HashSet<Transition> &abstraction_trs,
        utils::HashSet<OperatorID> &valid_ops,
        utils::HashSet<Transition> &valid_trs,
        utils::HashSet<Transition> &invalid_trs) const;

    void create_applicability_flaws(
        const State &state,
        const utils::HashSet<Transition> &valid_trs);

    bool create_deviation_flaws(
        const State &state,
        const State &next_state,
        const OperatorID &op_id,
        const utils::HashSet<Transition> &invalid_trs);

    void add_to_flaw_map(const Flaw &flaw);

    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;

public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task,
               const std::vector<int> &domain_sizes,
               const Abstraction &abstraction,
               const ShortestPaths &shortest_paths,
               bool debug);

    void get_flaws(std::map<int, std::vector<Flaw>> &flaw_map);

    std::unique_ptr<Flaw> search_for_flaws();

    void print_statistics() const;
};
}

#endif
