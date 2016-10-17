#ifndef COST_SATURATION_PROJECTION_GENERATOR_H
#define COST_SATURATION_PROJECTION_GENERATOR_H

#include "abstraction_generator.h"

namespace options {
class Options;
}

namespace pdbs {
class PatternCollectionGenerator;
}

namespace cost_saturation {
class ProjectionGenerator : public AbstractionGenerator {
    std::shared_ptr<pdbs::PatternCollectionGenerator> pattern_generator;

public:
    explicit ProjectionGenerator(const options::Options &opts);

    std::vector<std::unique_ptr<Abstraction>> generate_abstractions(
        const std::shared_ptr<AbstractTask> &task);
};
}

#endif
