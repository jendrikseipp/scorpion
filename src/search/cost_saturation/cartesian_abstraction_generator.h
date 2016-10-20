#ifndef COST_SATURATION_PROJECTION_GENERATOR_H
#define COST_SATURATION_PROJECTION_GENERATOR_H

#include "abstraction_generator.h"

namespace options {
class Options;
}

namespace cegar {
class SubtaskGenerator;
}

namespace cost_saturation {
class CartesianAbstractionGenerator : public AbstractionGenerator {
    const std::vector<std::shared_ptr<cegar::SubtaskGenerator>> subtask_generators;

public:
    explicit CartesianAbstractionGenerator(const options::Options &opts);

    std::vector<AbstractionAndStateMap> generate_abstractions(
        const std::shared_ptr<AbstractTask> &task);
};
}

#endif
