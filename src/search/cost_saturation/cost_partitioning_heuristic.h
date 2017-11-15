#ifndef COST_SATURATION_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class CostPartitioningHeuristic : public Heuristic {
    int compute_max_h_with_statistics(const std::vector<int> &local_state_ids) const;
    int compute_heuristic(const State &state);

protected:
    CostPartitionings h_values_by_order;
    Abstractions abstractions;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;
    const bool debug;

    std::vector<int> abstractions_per_generator;

    // For statistics.
    mutable std::vector<int> num_best_order;

    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    explicit CostPartitioningHeuristic(const options::Options &opts);
    virtual ~CostPartitioningHeuristic() override;

    virtual void print_statistics() const override;
};

extern void add_cost_partitioning_collection_options_to_parser(
    options::OptionParser &parser);

extern void prepare_parser_for_cost_partitioning_heuristic(
    options::OptionParser &parser);
}

#endif
