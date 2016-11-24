#ifndef COST_SATURATION_MERGE_AND_SHRINK_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_MERGE_AND_SHRINK_ABSTRACTION_GENERATOR_H

#include "abstraction_generator.h"

namespace options {
class Options;
}

namespace pdbs {
class PatternCollectionGenerator;
}

namespace cost_saturation {
class MergeAndShrinkAbstractionGenerator : public AbstractionGenerator {
    const std::shared_ptr<pdbs::PatternCollectionGenerator> pattern_generator;
    const bool debug;

public:
    explicit MergeAndShrinkAbstractionGenerator(const options::Options &opts);

    Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task);
};
}

#endif
