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

namespace utils {
class RandomNumberGenerator;
}

namespace successor_generator {
class SuccessorGenerator;
}

namespace cegar {
class Abstraction;
class ShortestPaths;
class AbstractState;
struct Split;

enum class PickFlaw {
    RANDOM_H_SINGLE,
    MIN_H_SINGLE,
    MAX_H_SINGLE,
    MIN_H_BATCH,
    MIN_H_BATCH_MAX_COVER,
};

class FlawSearch {
    const TaskProxy task_proxy;
    const std::vector<int> &domain_sizes;
    const Abstraction &abstraction;
    const ShortestPaths &shortest_paths;
    utils::RandomNumberGenerator &rng;
    PickFlaw pick;
    bool debug;

    // Search data
    std::unique_ptr<StateOpenList> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<SearchStatistics> statistics;
    const successor_generator::SuccessorGenerator &successor_generator;
    mutable std::unordered_map<int, int> concrete_state_to_abstract_state;

    // Flaw data
    int last_refined_abstract_state_id;
    int best_flaw_h;
    utils::HashMap<int, utils::HashSet<State>> flawed_states;

    // Statistics
    size_t num_searches;
    size_t num_overall_refined_flaws;
    size_t num_overall_expanded_concrete_states;
    utils::Timer timer;


protected:
    CartesianSet get_cartesian_set(const ConditionsProxy &conditions) const;

    int get_abstract_state_id(const State &state) const;

    int get_h_value(int abstract_state_id) const;

    int get_h_value(const State &state) const;

    void add_flaw(const State &state);

    bool is_f_optimal_transition(int abstract_state_id,
                                 const Transition &tr) const;

    const std::vector<Transition> &get_transitions(int abstract_state_id) const;

    void initialize();

    SearchStatus step();

    SearchStatus search_for_flaws();

    std::unique_ptr<Flaw>
    create_flaw(const State &state, int abstract_state_id);

    void create_all_flaws(const utils::HashSet<State> &states,
                          int abstract_state_id,
                          std::vector<Flaw> &flaws);

    std::unique_ptr<Flaw> get_random_single_flaw();

    std::unique_ptr<Flaw> get_single_flaw();

    std::unique_ptr<Flaw>
    get_min_h_batch_flaw(const std::pair<int, int> &new_state_ids);

    std::unique_ptr<Flaw>
    get_min_h_batch_max_cover_flaw(const std::pair<int, int> &new_state_ids);

public:
    FlawSearch(const std::shared_ptr<AbstractTask> &task,
               const std::vector<int> &domain_sizes,
               const Abstraction &abstraction,
               const ShortestPaths &shortest_paths,
               utils::RandomNumberGenerator &rng,
               PickFlaw pick,
               bool debug);

    std::unique_ptr<Flaw> get_flaw(const std::pair<int, int> &new_state_ids);

    void print_statistics() const;
};
}

#endif
