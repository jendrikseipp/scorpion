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

namespace priority_queues {
template<typename Value>
class AdaptiveQueue;
}

namespace utils {
class RandomNumberGenerator;
class Timer;
}

namespace pdbs {
class PartialStateTree;
class SequentialPatternGenerator;
struct TaskInfo;

enum class PatternOrder {
    RANDOM,
    PDB_SIZE_UP,
    PDB_SIZE_DOWN,
    ACTIVE_OPS_UP,
    ACTIVE_OPS_DOWN,
    PATTERN_UP,
    PATTERN_DOWN,
};

class PatternCollectionGeneratorFilteredSystematic : public PatternCollectionGenerator {
    const int max_pattern_size;
    const int max_pdb_size;
    const int max_collection_size;
    const int max_patterns;
    const double max_time;
    const double max_time_per_restart;
    const int max_evaluations_per_restart;
    const int max_total_evaluations;
    const bool saturate;
    const bool only_interesting_patterns;
    const bool ignore_useless_patterns;
    const bool store_orders;
    const bool store_dead_ends;
    const PatternOrder pattern_order;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;
    const bool debug;

    std::vector<std::vector<int>> relevant_operators_per_variable;

    int num_pattern_evaluations;

    std::unique_ptr<utils::Timer> pattern_computation_timer;
    std::unique_ptr<utils::Timer> projection_computation_timer;
    std::unique_ptr<utils::Timer> projection_evaluation_timer;

    bool select_systematic_patterns(
        const std::shared_ptr<AbstractTask> &task,
        const std::shared_ptr<cost_saturation::TaskInfo> &task_info,
        const TaskInfo &evaluator_task_info,
        SequentialPatternGenerator &pattern_generator,
        PartialStateTree *dead_ends,
        priority_queues::AdaptiveQueue<size_t> &pq,
        const std::shared_ptr<ProjectionCollection> &projections,
        PatternSet &pattern_set,
        PatternSet &patterns_checked_for_dead_ends,
        int64_t &collection_size,
        double overall_remaining_time);
public:
    explicit PatternCollectionGeneratorFilteredSystematic(const options::Options &opts);

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) override;
};
}

#endif
