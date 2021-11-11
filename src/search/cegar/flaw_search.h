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

    int min_flaw_h;
    utils::HashMap<int, utils::HashSet<State>> flawed_states;

    size_t num_searches;
    size_t num_overall_refined_flaws;
    size_t num_overall_expanded_concrete_states;
    utils::Timer timer;

    mutable std::unordered_map<int, int> concrete_state_to_abstract_state;

protected:
    int get_abstract_state_id(const State &state) const;

    int get_h_value(int abstract_state_id) const;

    int get_h_value(const State &state) const;

    void add_flaw(const State &state);

    bool is_f_optimal_transition(int abstract_state_id,
                                 const Transition &tr) const;

    const std::vector<Transition> &get_transitions(int abstract_state_id) const;

    void initialize();

    SearchStatus step();

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

    std::unique_ptr<Flaw>
    create_flaw(const State &state, int abstract_state_id);

    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;

public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task,
               const std::vector<int> &domain_sizes,
               const Abstraction &abstraction,
               const ShortestPaths &shortest_paths,
               bool debug);

    SearchStatus search_for_flaws();

    std::unique_ptr<Flaw> get_flaw(const std::pair<int, int> &new_state_ids);

    void print_statistics() const;
};
}

#endif
