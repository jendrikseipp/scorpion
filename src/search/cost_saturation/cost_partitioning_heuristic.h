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
class CostPartitionedHeuristic;
class CostPartitioningCollectionGenerator;

class CostPartitioningHeuristic : public Heuristic {
    Abstractions abstractions;
    const std::vector<CostPartitionedHeuristic> cp_heuristics;
    const bool debug;

    // For statistics.
    mutable std::vector<int> num_best_order;

    virtual int compute_heuristic(const State &state);

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    CostPartitioningHeuristic(
        const options::Options &opts,
        Abstractions &&abstractions,
        std::vector<CostPartitionedHeuristic> &&cp_heuristics);
    virtual ~CostPartitioningHeuristic() override;

    virtual void print_statistics() const override;
};

extern void add_cost_partitioning_collection_options_to_parser(
    options::OptionParser &parser);

extern void prepare_parser_for_cost_partitioning_heuristic(
    options::OptionParser &parser);

extern CostPartitioningCollectionGenerator get_cp_collection_generator_from_options(
    const options::Options &opts);
}

#endif
