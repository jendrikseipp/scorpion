#ifndef CEGAR_CEGAR_H
#define CEGAR_CEGAR_H

#include "flaw_search.h"
#include "split_selector.h"
#include "types.h"

#include "../task_proxy.h"

#include "../utils/countdown_timer.h"

#include <memory>

namespace utils {
class RandomNumberGenerator;
class LogProxy;
}

namespace cegar {
class Abstraction;
class AbstractSearch;
enum class DotGraphVerbosity;
class ShortestPaths;

/*
  Iteratively refine a Cartesian abstraction with counterexample-guided
  abstraction refinement (CEGAR).

  Store the abstraction, use AbstractSearch to find abstract solutions, find
  flaws, use SplitSelector to select splits in case of ambiguities and break
  spurious solutions.
*/
class CEGAR {
    const TaskProxy task_proxy;
    const std::vector<int> domain_sizes;
    const int max_states;
    const int max_non_looping_transitions;
    const SearchStrategy search_strategy;
    const PickFlawedAbstractState pick_flawed_abstract_state;

    std::unique_ptr<Abstraction> abstraction;
    std::unique_ptr<AbstractSearch> abstract_search;
    std::unique_ptr<ShortestPaths> shortest_paths;
    std::unique_ptr<FlawSearch> flaw_search;

    // Limit the time for building the abstraction.
    utils::CountdownTimer timer;

    utils::LogProxy &log;
    const DotGraphVerbosity dot_graph_verbosity;

    // Only used for logging progress.
    int old_abstract_solution_cost = -1;

    bool may_keep_refining() const;

    /*
      Map all states that can only be reached after reaching the goal
      fact to arbitrary goal states.

      We need this method only for landmark subtasks, but calling it
      for other subtasks with a single goal fact doesn't hurt and
      simplifies the implementation.
    */
    void separate_facts_unreachable_before_goal() const;

    // Build abstraction.
    void refinement_loop();

    void print_statistics() const;

public:
    CEGAR(
        const std::shared_ptr<AbstractTask> &task,
        int max_states,
        int max_non_looping_transitions,
        double max_time,
        PickFlawedAbstractState pick_flawed_abstract_state,
        PickSplit pick_split,
        PickSplit tiebreak_split,
        int max_concrete_states_per_abstract_state,
        int max_state_expansions,
        SearchStrategy search_strategy,
        utils::RandomNumberGenerator &rng,
        utils::LogProxy &log,
        DotGraphVerbosity dot_graph_verbosity);
    ~CEGAR();

    CEGAR(const CEGAR &) = delete;

    std::unique_ptr<Abstraction> extract_abstraction();
};
}

#endif
