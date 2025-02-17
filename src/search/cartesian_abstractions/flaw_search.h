#ifndef CARTESIAN_ABSTRACTIONS_FLAW_SEARCH_H
#define CARTESIAN_ABSTRACTIONS_FLAW_SEARCH_H

#include "flaw.h"
#include "split_selector.h"
#include "types.h"

// Needed for SearchStatus enum.
#include "../search_algorithm.h"

#include "../utils/logging.h"
#include "../utils/timer.h"

#include <stack>

namespace utils {
class CountdownTimer;
class LogProxy;
class RandomNumberGenerator;
}

namespace cartesian_abstractions {
class Abstraction;
class ShortestPaths;

// Variants from ICAPS 2022 paper (in order): FIRST, MIN_H, MAX_H, MIN_H, BATCH_MIN_H.
// See bottom of .cc file for documentation.
enum class PickFlawedAbstractState {
    FIRST,
    FIRST_ON_SHORTEST_PATH,
    RANDOM,
    MIN_H,
    MAX_H,
    BATCH_MIN_H
};

class FlawSearch {
    TaskProxy task_proxy;
    const std::vector<int> domain_sizes;
    const Abstraction &abstraction;
    const ShortestPaths &shortest_paths;
    const SplitSelector split_selector;
    utils::RandomNumberGenerator &rng;
    const PickFlawedAbstractState pick_flawed_abstract_state;
    const int max_concrete_states_per_abstract_state;
    const int max_state_expansions;
    mutable utils::LogProxy log;
    mutable utils::LogProxy silent_log;  // For concrete search space.

    static const int MISSING = -1;

    // Search data
    std::stack<StateID> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<PerStateInformation<int>> cached_abstract_state_ids;

    // Flaw data
    FlawedState last_refined_flawed_state;
    Cost best_flaw_h;
    FlawedStates flawed_states;

    // Statistics
    int num_searches;
    int num_overall_expanded_concrete_states;
    int max_expanded_concrete_states;
    utils::Timer flaw_search_timer;
    utils::Timer compute_splits_timer;
    utils::Timer pick_split_timer;

    int get_abstract_state_id(const State &state) const;
    Cost get_h_value(int abstract_state_id) const;
    void add_flaw(int abs_id, const State &state);
    OptimalTransitions get_f_optimal_transitions(int abstract_state_id) const;

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
        const Abstraction &abstraction,
        const ShortestPaths &shortest_paths,
        utils::RandomNumberGenerator &rng,
        PickFlawedAbstractState pick_flawed_abstract_state,
        PickSplit pick_split,
        PickSplit tiebreak_split,
        int max_concrete_states_per_abstract_state,
        int max_state_expansions,
        const utils::LogProxy &log);

    std::unique_ptr<Split> get_split(const utils::CountdownTimer &cegar_timer);
    std::unique_ptr<Split> get_split_legacy(const Solution &solution);

    void print_statistics() const;
};
}

#endif
