#ifndef COST_SATURATION_CARTESIAN_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_CARTESIAN_ABSTRACTION_GENERATOR_H

#include "abstraction_generator.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace cartesian_abstractions {
class Abstraction;
enum class DotGraphVerbosity;
enum class PickFlawedAbstractState;
enum class PickSplit;
class SubtaskGenerator;
}

namespace utils {
class CountdownTimer;
class RandomNumberGenerator;
}

namespace cost_saturation {
class CartesianAbstractionGenerator : public AbstractionGenerator {
    const std::vector<std::shared_ptr<cartesian_abstractions::SubtaskGenerator>> subtask_generators;
    const int max_states;
    const int max_transitions;
    const double max_time;
    const cartesian_abstractions::PickFlawedAbstractState pick_flawed_abstract_state;
    const cartesian_abstractions::PickSplit pick_split;
    const cartesian_abstractions::PickSplit tiebreak_split;
    const int max_concrete_states_per_abstract_state;
    const int max_state_expansions;
    const int extra_memory_padding_mb;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;
    const cartesian_abstractions::DotGraphVerbosity dot_graph_verbosity;

    int num_states;
    int num_transitions;

    bool has_reached_resource_limit(const utils::CountdownTimer &timer) const;

    std::unique_ptr<cartesian_abstractions::Abstraction> build_abstraction_for_subtask(
        const std::shared_ptr<AbstractTask> &subtask,
        int remaining_subtasks,
        const utils::CountdownTimer &timer);

    void build_abstractions_for_subtasks(
        const std::vector<std::shared_ptr<AbstractTask>> &subtasks,
        const utils::CountdownTimer &timer,
        Abstractions &abstractions);

public:
    explicit CartesianAbstractionGenerator(const plugins::Options &opts);

    Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends) override;
};
}

#endif
