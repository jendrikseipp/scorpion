#ifndef CEGAR_FLAW_SELECTOR_H
#define CEGAR_FLAW_SELECTOR_H

#include "cartesian_set.h"
#include "types.h"

#include "../task_proxy.h"

#include <memory>

namespace cegar {
class AbstractState;
class Abstraction;
struct Split;

enum class FlawStrategy {
    BACKTRACK,
    OPTIMISTIC,
    ORIGINAL,
    PESSIMISTIC,
    RANDOM
};

struct Flaw {
    // Last concrete and abstract state reached while tracing solution.
    State concrete_state;
    const AbstractState &current_abstract_state;
    // Hypothetical Cartesian set we would have liked to reach.
    CartesianSet desired_cartesian_set;

    Flaw(State &&concrete_state, const AbstractState &current_abstract_state,
         CartesianSet &&desired_cartesian_set);

    std::vector<Split> get_possible_splits() const;
};

class FlawSelector {
    const std::shared_ptr<AbstractTask> task;
    const TaskProxy task_proxy;
    FlawStrategy flaw_strategy;
    bool debug;

    mutable size_t depth;
    mutable size_t num_runs;

    std::unique_ptr<Flaw> find_first_flaw(const Abstraction &abstraction,
                                          const std::vector<int> &domain_sizes,
                                          const Solution &solution) const;

    std::unique_ptr<Flaw>
    find_greedy_wildcard_flaw(const Abstraction &abstraction,
                              const std::vector<int> &domain_sizes,
                              const Solution &solution) const;



    bool are_wildcard_tr(const Transition &tr1, const Transition &tr2) const;

public:
    FlawSelector(const std::shared_ptr<AbstractTask> &task,
                 FlawStrategy flaw_strategy, bool debug);
    ~FlawSelector();

    /* Try to convert the abstract solution into a concrete trace. Return the
       encountered flaw or nullptr if there is no flaw. */
    std::unique_ptr<Flaw> find_flaw(const Abstraction &abstraction,
                                    const std::vector<int> &domain_sizes,
                                    const Solution &solution) const;

    void print_statistics() const;
};
} // namespace cegar

#endif
