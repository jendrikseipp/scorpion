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
    const std::shared_ptr<pdbs::PatternCollectionGenerator> pattern_generator;
    const bool dominance_pruning;
    const bool combine_labels;
    const bool create_complete_transition_system;
    const bool use_add_after_delete_semantics;

public:
    explicit ProjectionGenerator(const options::Options &opts);

    Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends);
};
}

#endif
