#ifndef COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H
#define COST_SATURATION_MAX_COST_PARTITIONING_HEURISTIC_H

#include "types.h"

#include "../heuristic.h"

#include <vector>

namespace options {
class Options;
}

namespace cost_saturation {
class CostPartitioningHeuristicCollectionGenerator;
class CostPartitioningHeuristic;

/*
  Compute the maximum over multiple cost partitioning heuristics.
*/
class MaxCostPartitioningHeuristic : public Heuristic {
    Abstractions abstractions;
    const std::vector<CostPartitioningHeuristic> cp_heuristics;

    // For statistics.
    mutable std::vector<int> num_best_order;

    int compute_heuristic(const State &state) const;

protected:
    virtual int compute_heuristic(const GlobalState &global_state) override;

public:
    MaxCostPartitioningHeuristic(
        const options::Options &opts,
        Abstractions &&abstractions,
        std::vector<CostPartitioningHeuristic> &&cp_heuristics);

    virtual void print_statistics() const override;
};

extern std::shared_ptr<Heuristic> get_max_cp_heuristic(
    options::OptionParser &parser, CPFunction cp_function);

extern void add_order_options_to_parser(
    options::OptionParser &parser);

extern void prepare_parser_for_cost_partitioning_heuristic(
    options::OptionParser &parser);

extern CostPartitioningHeuristicCollectionGenerator
get_cp_heuristic_collection_generator_from_options(
    const options::Options &opts);
}

#endif
