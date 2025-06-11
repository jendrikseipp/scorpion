#ifndef COST_SATURATION_PROJECTION_GENERATOR_H
#define COST_SATURATION_PROJECTION_GENERATOR_H

#include "abstraction_generator.h"

namespace pdbs {
class PatternCollectionGenerator;
}

namespace cost_saturation {
class ProjectionGenerator : public AbstractionGenerator {
    const std::shared_ptr<pdbs::PatternCollectionGenerator> pattern_generator;
    const bool dominance_pruning;
    const bool combine_labels;
    const bool create_complete_transition_system;
    const int min_ops_per_label;

    int num_transitions;
    int num_total_non_label_transitions;
    int num_total_reused_label_transitions;
    int num_total_label_transitions;


public:
    ProjectionGenerator(
        const std::shared_ptr<pdbs::PatternCollectionGenerator> &patterns,
        bool dominance_pruning,
        bool combine_labels,
        bool create_complete_transition_system,
        int min_ops_per_label,
        utils::Verbosity verbosity);

    virtual Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends) override;
};
}

#endif
