#ifndef PDBS_PATTERN_COLLECTION_GENERATOR_SYSTEMATIC_H
#define PDBS_PATTERN_COLLECTION_GENERATOR_SYSTEMATIC_H

#include "pattern_generator.h"
#include "types.h"

#include "../utils/hash.h"

#include <cstdlib>
#include <memory>
#include <unordered_set>
#include <vector>

class TaskProxy;

namespace causal_graph {
class CausalGraph;
}

namespace options {
class Options;
}

namespace utils {
class CountdownTimer;
}

namespace pdbs {
class CanonicalPDBsHeuristic;

using PatternHandler = std::function<bool (const Pattern &)>;

// Invariant: patterns are always sorted.
class PatternCollectionGeneratorSystematic : public PatternCollectionGenerator {
    using PatternSet = utils::HashSet<Pattern>;

    const size_t max_pattern_size;
    const bool only_interesting_patterns;
    std::shared_ptr<PatternCollection> patterns;
    PatternSet pattern_set;  // Cleared after pattern computation.

    void enqueue_pattern_if_new(
        const Pattern &pattern, const PatternHandler &handle_pattern);
    void compute_eff_pre_neighbors(const causal_graph::CausalGraph &cg,
                                   const Pattern &pattern,
                                   std::vector<int> &result) const;
    void compute_connection_points(const causal_graph::CausalGraph &cg,
                                   const Pattern &pattern,
                                   std::vector<int> &result) const;

    void build_sga_patterns(
        const TaskProxy &task_proxy,
        const causal_graph::CausalGraph &cg,
        const PatternHandler &handle_pattern);
    void build_patterns(
        const TaskProxy &task_proxy,
        const PatternHandler &handle_pattern = nullptr,
        const utils::CountdownTimer *timer = nullptr);
    void build_patterns_naive(const TaskProxy &task_proxy);
public:
    explicit PatternCollectionGeneratorSystematic(const options::Options &opts);

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) override;

    void generate(
        const std::shared_ptr<AbstractTask> &task,
        const PatternHandler &handle_pattern,
        const utils::CountdownTimer &timer);
};
}

#endif
