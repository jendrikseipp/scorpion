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
class Feature;
}

namespace utils {
class CountdownTimer;
}

namespace pdbs {
class CanonicalPDBsHeuristic;

/*
naive: All patterns of a given size (with distinct variables) are interesting.

interesting-general: A pattern P is interesting if
1. the subgraph of the causal graph induced by P is weakly connected, and
2. the full causal graph of the original task contains a directed path
   via precondition arcs from each node in P to some goal variable node
   (possible not in P).

interesting-non-negative: A pattern P is interesting if
1. the subgraph of the causal graph induced by P is weakly connected, and
2. the subgraph of the causal graph induced by P contains a directed path
   via precondition arcs from each node to some goal variable node.
*/
enum class PatternType {
    NAIVE,
    INTERESTING_GENERAL,
    INTERESTING_NON_NEGATIVE,
};

// Invariant: patterns are always sorted.
class PatternCollectionGeneratorSystematic : public PatternCollectionGenerator {
    using PatternSet = utils::HashSet<Pattern>;

    const size_t max_pattern_size;
    const PatternType pattern_type;
    std::shared_ptr<PatternCollection> patterns;
    PatternSet pattern_set;  // Cleared after pattern computation.

    void enqueue_pattern_if_new(const Pattern &pattern);
    void compute_eff_pre_neighbors(const causal_graph::CausalGraph &cg,
                                   const Pattern &pattern,
                                   std::vector<int> &result) const;
    std::vector<int> compute_variables_with_precondition_path_to_goal(
        const TaskProxy &task_proxy, const causal_graph::CausalGraph &cg) const;
    void compute_connection_points(const causal_graph::CausalGraph &cg,
                                   const Pattern &pattern,
                                   std::vector<int> &result) const;

    void build_sga_patterns(
        const TaskProxy &task_proxy,
        const causal_graph::CausalGraph &cg);
    void build_patterns(
        const TaskProxy &task_proxy,
        const utils::CountdownTimer *timer = nullptr);
    void build_patterns_naive(
        const TaskProxy &task_proxy,
        const utils::CountdownTimer *timer = nullptr);
    virtual std::string name() const override;
    virtual PatternCollectionInformation compute_patterns(
        const std::shared_ptr<AbstractTask> &task) override;
public:
    PatternCollectionGeneratorSystematic(
        int pattern_max_size, PatternType pattern_type,
        utils::Verbosity verbosity);

    void generate(
        const std::shared_ptr<AbstractTask> &task,
        const PatternHandler &handle_pattern,
        const utils::CountdownTimer &timer);
};

extern void add_pattern_type_option(plugins::Feature &feature);
}

#endif
