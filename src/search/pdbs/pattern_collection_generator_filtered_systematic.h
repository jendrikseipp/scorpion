#ifndef PDBS_PATTERN_COLLECTION_GENERATOR_FILTERED_SYSTEMATIC_H
#define PDBS_PATTERN_COLLECTION_GENERATOR_FILTERED_SYSTEMATIC_H

#include "pattern_generator.h"
#include "types.h"

#include <memory>

namespace options {
class Options;
}

namespace pdbs {
enum class ScoringFunction {
    INIT_H,
    AVG_H,
    INIT_H_PER_COSTS,
    AVG_H_PER_COSTS,
    INIT_H_PER_SIZE,
    AVG_H_PER_SIZE,
};

class PatternCollectionGeneratorFilteredSystematic : public PatternCollectionGenerator {
    const int max_pattern_size;
    const int max_collection_size;
    const int max_patterns;
    const double max_time;
    const bool keep_best;
    const ScoringFunction scoring_function;
    const bool saturate;
    const bool debug;

    double rate_projection(
        const cost_saturation::Projection &projection,
        const std::vector<int> &costs,
        const State &initial_state);

    PatternCollectionInformation select_systematic_patterns(
        const std::shared_ptr<AbstractTask> &task);
public:
    explicit PatternCollectionGeneratorFilteredSystematic(const options::Options &opts);

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) override;
};
}

#endif
