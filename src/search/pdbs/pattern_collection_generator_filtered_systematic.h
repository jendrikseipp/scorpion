#ifndef PDBS_PATTERN_COLLECTION_GENERATOR_FILTERED_SYSTEMATIC_H
#define PDBS_PATTERN_COLLECTION_GENERATOR_FILTERED_SYSTEMATIC_H

#include "pattern_generator.h"
#include "types.h"

#include <memory>

namespace options {
class Options;
}

namespace pdbs {
class PatternCollectionGeneratorFilteredSystematic : public PatternCollectionGenerator {
    int pattern_max_size;
    const double max_time;
    const bool debug;

    void select_systematic_patterns(
        const std::shared_ptr<AbstractTask> &task,
        int pattern_max_size,
        PatternCollection &patterns);
public:
    explicit PatternCollectionGeneratorFilteredSystematic(const options::Options &opts);

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) override;
};
}

#endif
