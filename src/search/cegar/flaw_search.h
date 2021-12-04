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

#include <queue>

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

enum class PickFlaw {
    SINGLE_PATH,
    RANDOM_H_SINGLE,
    MIN_H_SINGLE,
    MAX_H_SINGLE,
    MIN_H_BATCH,
    MIN_H_BATCH_MULTI_SPLIT
};

class FlawSearch {
    const TaskProxy task_proxy;
    const std::vector<int> &domain_sizes;
    const Abstraction &abstraction;
    const ShortestPaths &shortest_paths;
    const SplitSelector split_selector;
    utils::RandomNumberGenerator &rng;
    const PickFlaw pick_flaw;
    const bool debug;

    static const int MISSING = -1;

    // Search data
    std::queue<StateID> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<PerStateInformation<int>> abstract_state_ids;

    // Flaw data
    FlawedState last_refined_flawed_state;
    int best_flaw_h;
    FlawedStates flawed_states;

    // Statistics
    size_t num_searches;
    size_t num_overall_expanded_concrete_states;
    utils::Timer flaw_search_timer;
    utils::Timer pick_split_timer;

    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;
    int get_abstract_state_id(const State &state) const;
    int get_h_value(int abstract_state_id) const;
    void add_flaw(int abs_id, const State &state);
    bool is_f_optimal_transition(int abstract_state_id, const Transition &tr) const;
    const std::vector<Transition> &get_transitions(int abstract_state_id) const;

    void initialize();
    SearchStatus step();
    SearchStatus search_for_flaws(const utils::CountdownTimer &cegar_timer);

    std::unique_ptr<Split> create_split(
        const std::vector<State> &states, int abstract_state_id);

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
        bool debug);

    std::unique_ptr<Split> get_split(const utils::CountdownTimer &cegar_timer);

    void print_statistics() const;
};
}

#endif
