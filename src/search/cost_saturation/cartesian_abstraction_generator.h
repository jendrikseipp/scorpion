#ifndef COST_SATURATION_CARTESIAN_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_CARTESIAN_ABSTRACTION_GENERATOR_H

#include "abstraction_generator.h"

namespace options {
class Options;
}

namespace cegar {
class SubtaskGenerator;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cost_saturation {
class CartesianAbstractionGenerator : public AbstractionGenerator {
    const std::vector<std::shared_ptr<cegar::SubtaskGenerator>> subtask_generators;
    const int max_transitions;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

public:
    explicit CartesianAbstractionGenerator(const options::Options &opts);

    Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task);
};
}

#endif
