#ifndef PDBS_PATTERN_COLLECTION_GENERATOR_FILTERED_SYSTEMATIC_H
#define PDBS_PATTERN_COLLECTION_GENERATOR_FILTERED_SYSTEMATIC_H

#include "pattern_generator.h"
#include "types.h"

#include <memory>

namespace cost_saturation {
class TaskInfo;
}

namespace options {
class Options;
}

namespace pdbs {
class PatternCollectionGeneratorFilteredSystematic : public PatternCollectionGenerator {
    using PatternSet = utils::HashSet<Pattern>;

    const int max_pattern_size;
    const int max_pdb_size;
    const int max_collection_size;
    const int max_patterns;
    const double max_time;
    const double max_time_per_restart;
    const bool saturate;
    const bool debug;

    bool select_systematic_patterns(
        const std::shared_ptr<AbstractTask> &task,
        const std::shared_ptr<cost_saturation::TaskInfo> &task_info,
        const std::shared_ptr<ProjectionCollection> &projections,
        PatternSet &pattern_set,
        int64_t &collection_size,
        double overall_remaining_time);
public:
    explicit PatternCollectionGeneratorFilteredSystematic(const options::Options &opts);

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) override;
};
}

#endif
