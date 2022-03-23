#ifndef CEGAR_FLAW_SEARCH_H
#define CEGAR_FLAW_SEARCH_H

#include "cartesian_set.h"
#include "flaw.h"
#include "split_selector.h"
#include "types.h"

#include "../open_list.h"
#include "../search_engine.h"

#include "../utils/timer.h"
#include "../utils/hash.h"

#include <parallel_hashmap/phmap.h>

#include <stack>

namespace utils {
class CountdownTimer;
class RandomNumberGenerator;
}

namespace successor_generator {
class SuccessorGenerator;
}

namespace cegar {
class Abstraction;
class ShortestPaths;
class AbstractState;

// ICAPS 2022 configuration (in order): FIRST, MIN_H, MAX_H, MIN_H BATCH_MIN_H
enum class PickFlaw {
    // FIRST configuration of ICAPS 2022 paper 
    // Considers first encountered abstract state + a random concrete state
    // Considers 
    FIRST, 
    // Legacy code: following a "random" solution; not using flaw search
    // Considers first encountered abstract state + a random concrete state
    FIRST_ON_SHORTEST_PATH, 
    // Collects all all flawed abstract states
    // Considers a random abstract state + a random concrete state
    RANDOM,
    // Collects all all flawed abstract states
    // Considers a random abstract state with min h + a random concrete state
    MIN_H,
    // Collects all all flawed abstract states
    // Considers a random abstract state with max h + a random concrete state
    MAX_H,
    // Collects all all flawed abstract states and iteratively refines them (increasing h value)
    // Does only restart if we refined all "possible" flawed abstract states
    // For each abstract state all concrete states are considered
    BATCH_MIN_H
};

using OptimalTransitions = phmap::flat_hash_map<int, std::vector<int>>;

class FlawSearch {
    const TaskProxy task_proxy;
    mutable utils::LogProxy log;
    const std::vector<int> &domain_sizes;
    const Abstraction &abstraction;
    const ShortestPaths &shortest_paths;
    const SplitSelector split_selector;
    utils::RandomNumberGenerator &rng;
    const PickFlaw pick_flaw;
    const int max_concrete_states_per_abstract_state;
    const int max_state_expansions;
    const bool debug;

    static const int MISSING = -1;

    // Search data
    std::stack<StateID> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<PerStateInformation<int>> abstract_state_ids;

    // Flaw data
    FlawedState last_refined_flawed_state;
    Cost best_flaw_h;
    FlawedStates flawed_states;

    // Statistics
    size_t num_searches;
    size_t num_overall_expanded_concrete_states;
    size_t max_expanded_concrete_states;
    utils::Timer flaw_search_timer;
    utils::Timer compute_splits_timer;
    utils::Timer pick_split_timer;

    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;
    int get_abstract_state_id(const State &state) const;
    Cost get_h_value(int abstract_state_id) const;
    void add_flaw(int abs_id, const State &state);
    OptimalTransitions get_f_optimal_transitions(int abstract_state_id) const;
    const std::vector<Transition> &get_transitions(int abstract_state_id) const;

    void initialize();
    SearchStatus step();
    SearchStatus search_for_flaws(const utils::CountdownTimer &cegar_timer);

    std::unique_ptr<Split> create_split(
        const std::vector<StateID> &state_ids, int abstract_state_id);

    FlawedState get_flawed_state_with_min_h();
    std::unique_ptr<Split> get_single_split(const utils::CountdownTimer &cegar_timer);
    std::unique_ptr<Split> get_min_h_batch_split(const utils::CountdownTimer &cegar_timer);

public:
    FlawSearch(
        const std::shared_ptr<AbstractTask> &task,
        const std::vector<int> &domain_sizes,
        const Abstraction &abstraction,
        const ShortestPaths &shortest_paths,
        utils::RandomNumberGenerator &rng,
        PickFlaw pick_flaw,
        PickSplit pick_split,
        PickSplit tiebreak_split,
        int max_concrete_states_per_abstract_state,
        int max_state_expansions,
        bool debug);

    std::unique_ptr<Split> get_split(const utils::CountdownTimer &cegar_timer);
    std::unique_ptr<Split> get_split_legacy(const Solution &solution);

    PickFlaw get_pick_flaw_mode() const {
        return pick_flaw;
    }

    void print_statistics() const;
};
}

#endif
