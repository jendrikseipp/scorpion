#ifndef PDBS_PATTERN_COLLECTION_GENERATOR_SCP_H
#define PDBS_PATTERN_COLLECTION_GENERATOR_SCP_H

#include "pattern_generator.h"
#include "types.h"

#include <memory>
#include <vector>

namespace cost_saturation {
class Projection;
}

namespace options {
class Options;
}

namespace utils {
class CountdownTimer;
class RandomNumberGenerator;
}

namespace sampling {
class RandomWalkSampler;
}

namespace pdbs {
class PatternDatabase;

class PatternCollectionGeneratorSCP : public PatternCollectionGenerator {
    const int pdb_max_size;
    const int collection_max_size;
    const int num_samples;
    const double min_improvement;
    const double max_time;
    const bool debug;
    std::shared_ptr<utils::RandomNumberGenerator> rng;

    std::vector<std::unique_ptr<cost_saturation::Projection>> projections;
    std::vector<std::vector<int>> cost_partitioned_h_values; // TODO: Could store bools instead.

    std::vector<std::vector<int>> relevant_neighbours;
    std::vector<int> goal_vars;

    std::vector<State> samples;
    std::vector<int> sample_h_values;
    int init_h;

    void sample_states(
        const sampling::RandomWalkSampler &sampler,
        int init_h,
        std::vector<State> &samples);
    double evaluate_pdb(const PatternDatabase &pdb);
    std::pair<int, double> compute_best_variable_to_add(
        const TaskProxy &task_proxy, const std::vector<int> &costs,
        const Pattern &pattern, int num_states, int max_states,
        const utils::CountdownTimer &timer);
    Pattern compute_next_pattern(
        const TaskProxy &task_proxy, const std::vector<int> &costs, int max_states,
        const utils::CountdownTimer &timer);
    int compute_current_heuristic(const State &state) const;
    std::vector<int> get_connected_variables(const Pattern &pattern);
public:
    explicit PatternCollectionGeneratorSCP(const options::Options &opts);

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) override;
};
}

#endif
