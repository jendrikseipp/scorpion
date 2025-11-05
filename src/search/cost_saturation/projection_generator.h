#ifndef COST_SATURATION_PROJECTION_GENERATOR_H
#define COST_SATURATION_PROJECTION_GENERATOR_H

#include "abstraction_generator.h"
#include "types.h"

namespace pdbs {
class PatternCollectionGenerator;
}

namespace cost_saturation {
class ProjectionGenerator : public AbstractionGenerator {
    const std::shared_ptr<pdbs::PatternCollectionGenerator> pattern_generator;
    const bool dominance_pruning;
    const bool combine_labels;
    const TransitionSystemType transition_type;

public:
    ProjectionGenerator(
        const std::shared_ptr<pdbs::PatternCollectionGenerator> &patterns,
        bool dominance_pruning, bool combine_labels,
        TransitionSystemType transition_type, utils::Verbosity verbosity);

    virtual Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends) override;
};
}

#endif
