#ifndef CEGAR_FLAW_SELECTOR_H
#define CEGAR_FLAW_SELECTOR_H

#include "cartesian_set.h"
#include "types.h"

#include "../task_proxy.h"

#include <memory>

namespace utils {
class RandomNumberGenerator;
}

namespace cegar {
class AbstractState;
class Abstraction;
struct Split;

enum class FlawStrategy {
    BACKTRACK_OPTIMISTIC,
    BACKTRACK_OPTIMISTIC_SLOW,
    BACKTRACK_PESSIMISTIC,
    BACKTRACK_PESSIMISTIC_SLOW,
    OPTIMISTIC,
    OPTIMISTIC_SLOW,
    ORIGINAL,
    PESSIMISTIC,
    PESSIMISTIC_SLOW,
    RANDOM
};

enum class FlawReason {
    NOT_APPLICABLE,
    PATH_DEVIATION,
    GOAL_TEST
};

struct Flaw {
    // Last concrete and abstract state reached while tracing solution.
    State concrete_state;
    const AbstractState &current_abstract_state;
    // Hypothetical Cartesian set we would have liked to reach.
    CartesianSet desired_cartesian_set;

    FlawReason flaw_reason;
    Solution flawed_solution;

    Flaw(State &&concrete_state, const AbstractState &current_abstract_state,
         CartesianSet &&desired_cartesian_set, FlawReason reason,
         const Solution &flawed_solution);

    std::vector<Split> get_possible_splits() const;
};

class FlawSelector {
    const std::shared_ptr<AbstractTask> task;
    const TaskProxy task_proxy;
    FlawStrategy flaw_strategy;
    bool debug;

    std::unique_ptr<Flaw>
    find_flaw_backtrack_optimistic(const Abstraction &abstraction,
                                   const std::vector<int> &domain_sizes,
                                   const Solution &solution,
                                   utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_backtrack_optimistic_slow(const Abstraction &abstraction,
                                        const std::vector<int> &domain_sizes,
                                        const Solution &solution,
                                        utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_backtrack_pessimistic(const Abstraction &abstraction,
                                    const std::vector<int> &domain_sizes,
                                    const Solution &solution,
                                    utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_backtrack_pessimistic_slow(const Abstraction &abstraction,
                                         const std::vector<int> &domain_sizes,
                                         const Solution &solution,
                                         utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw> find_flaw_original(const Abstraction &abstraction,
                                             const std::vector<int> &domain_sizes,
                                             const Solution &solution, bool rnd_choice,
                                             utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_optimistic(const Abstraction &abstraction,
                         const std::vector<int> &domain_sizes,
                         const Solution &solution,
                         utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_optimistic_slow(const Abstraction &abstraction,
                              const std::vector<int> &domain_sizes,
                              const Solution &solution,
                              utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_pessimistic(const Abstraction &abstraction,
                          const std::vector<int> &domain_sizes,
                          const Solution &solution,
                          utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw>
    find_flaw_pessimistic_slow(const Abstraction &abstraction,
                               const std::vector<int> &domain_sizes,
                               const Solution &solution,
                               utils::RandomNumberGenerator &rng) const;

    bool are_wildcard_tr(const Transition &tr1, const Transition &tr2) const;
    void get_wildcard_trs(const Abstraction &abstraction,
                          const AbstractState *abstract_state,
                          const Transition &base_tr,
                          std::vector<Transition> &wildcard_trs) const;

    bool is_flaw_better(const std::unique_ptr<Flaw> &flaw1,
                        const std::unique_ptr<Flaw> &flaw2) const;

    CartesianSet get_cartesian_set(const std::vector<int> &domain_sizes,
                                   const ConditionsProxy &conditions) const;

public:
    FlawSelector(const std::shared_ptr<AbstractTask> &task,
                 FlawStrategy flaw_strategy, bool debug);
    ~FlawSelector();

    /* Try to convert the abstract solution into a concrete trace. Return the
       encountered flaw or nullptr if there is no flaw. */
    std::unique_ptr<Flaw> find_flaw(const Abstraction &abstraction,
                                    const std::vector<int> &domain_sizes,
                                    const Solution &solution,
                                    utils::RandomNumberGenerator &rng) const;

    void print_statistics() const;
};
} // namespace cegar

#endif
