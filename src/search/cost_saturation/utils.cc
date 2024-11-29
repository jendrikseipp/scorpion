#include "utils.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "unsolvability_heuristic.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/rng_options.h"

#include <cassert>
#include <numeric>

using namespace std;

namespace cost_saturation {
Abstractions generate_abstractions(
    const shared_ptr<AbstractTask> &task,
    const vector<shared_ptr<AbstractionGenerator>> &abstraction_generators,
    DeadEnds *dead_ends) {
    Abstractions abstractions;
    vector<int> abstractions_per_generator;
    for (const shared_ptr<AbstractionGenerator> &generator : abstraction_generators) {
        int abstractions_before = abstractions.size();
        for (auto &abstraction : generator->generate_abstractions(task, dead_ends)) {
            abstractions.push_back(move(abstraction));
        }
        abstractions_per_generator.push_back(abstractions.size() - abstractions_before);
    }
    utils::g_log << "Abstractions: " << abstractions.size() << endl;
    utils::g_log << "Abstractions per generator: " << abstractions_per_generator << endl;
    return abstractions;
}

AbstractionFunctions extract_abstraction_functions_from_useful_abstractions(
    const vector<CostPartitioningHeuristic> &cp_heuristics,
    const UnsolvabilityHeuristic *unsolvability_heuristic,
    Abstractions &abstractions) {
    int num_abstractions = abstractions.size();

    // Collect IDs of useful abstractions.
    vector<bool> useful_abstractions(num_abstractions, false);
    if (unsolvability_heuristic) {
        unsolvability_heuristic->mark_useful_abstractions(useful_abstractions);
    }
    for (const auto &cp_heuristic : cp_heuristics) {
        cp_heuristic.mark_useful_abstractions(useful_abstractions);
    }

    AbstractionFunctions abstraction_functions;
    abstraction_functions.reserve(num_abstractions);
    for (int i = 0; i < num_abstractions; ++i) {
        if (useful_abstractions[i]) {
            abstraction_functions.push_back(
                abstractions[i]->extract_abstraction_function());
        } else {
            abstraction_functions.push_back(nullptr);
        }
    }

    int num_useless_abstractions = count(
        abstraction_functions.begin(), abstraction_functions.end(), nullptr);
    int num_useful_abstractions = num_abstractions - num_useless_abstractions;
    utils::g_log << "Useful abstractions: " << num_useful_abstractions << "/"
                 << num_abstractions << " = "
                 << static_cast<double>(num_useful_abstractions) / num_abstractions
                 << endl;

    return abstraction_functions;
}

Order get_default_order(int num_abstractions) {
    vector<int> indices(num_abstractions);
    iota(indices.begin(), indices.end(), 0);
    return indices;
}

bool is_sum_within_range(int a, int b) {
    return (b >= 0 && a <= numeric_limits<int>::max() - b) ||
           (b < 0 && a >= numeric_limits<int>::min() - b);
}

int left_addition(int a, int b) {
    if (a == -INF || a == INF) {
        return a;
    } else if (b == -INF || b == INF) {
        return b;
    } else {
        assert(is_sum_within_range(a, b));
        return a + b;
    }
}

int compute_max_h(
    const CPHeuristics &cp_heuristics,
    const vector<int> &abstract_state_ids,
    vector<int> *num_best_order) {
    int max_h = 0;
    int best_id = -1;
    int current_id = 0;
    for (const CostPartitioningHeuristic &cp_heuristic : cp_heuristics) {
        int sum_h = cp_heuristic.compute_heuristic(abstract_state_ids);
        if (sum_h > max_h) {
            max_h = sum_h;
            best_id = current_id;
        }
        if (max_h == INF) {
            break;
        }
        ++current_id;
    }
    assert(max_h >= 0);

    if (num_best_order) {
        num_best_order->resize(cp_heuristics.size(), 0);
        if (best_id != -1) {
            ++(*num_best_order)[best_id];
        }
    }

    return max_h;
}

void reduce_costs(vector<int> &remaining_costs, const vector<int> &saturated_costs) {
    assert(remaining_costs.size() == saturated_costs.size());
    for (size_t i = 0; i < remaining_costs.size(); ++i) {
        int &remaining = remaining_costs[i];
        int saturated = saturated_costs[i];
        assert(remaining >= 0);
        assert(saturated <= remaining);
        if (remaining == INF) {
            // Left addition: x - y = x for all values y if x is infinite.
        } else if (saturated == -INF) {
            remaining = INF;
        } else {
            assert(saturated != INF);
            remaining -= saturated;
        }
        assert(remaining >= 0);
    }
}

void add_order_options(plugins::Feature &feature) {
    feature.add_option<shared_ptr<OrderGenerator>>(
        "orders",
        "order generator",
        "greedy_orders()");
    feature.add_option<int>(
        "max_orders",
        "maximum number of orders",
        "infinity",
        plugins::Bounds("0", "infinity"));
    feature.add_option<int>(
        "max_size",
        "maximum heuristic size in KiB",
        "infinity",
        plugins::Bounds("0", "infinity"));
    feature.add_option<double>(
        "max_time",
        "maximum time in seconds for finding orders",
        "200",
        plugins::Bounds("0", "infinity"));
    feature.add_option<bool>(
        "diversify",
        "only keep orders that have a higher heuristic value than all previous "
        "orders for any of the samples",
        "true");
    feature.add_option<int>(
        "samples",
        "number of samples for diversification",
        "1000",
        plugins::Bounds("1", "infinity"));
    feature.add_option<double>(
        "max_optimization_time",
        "maximum time in seconds for optimizing each order with hill climbing",
        "2",
        plugins::Bounds("0", "infinity"));
    utils::add_rng_options_to_feature(feature);
}

shared_ptr<CostPartitioningHeuristicCollectionGenerator>
get_cp_heuristic_collection_generator_from_options(const plugins::Options &opts) {
    return plugins::make_shared_from_arg_tuples<CostPartitioningHeuristicCollectionGenerator>(
        opts.get<shared_ptr<OrderGenerator>>("orders"),
        opts.get<int>("max_orders"),
        opts.get<int>("max_size"),
        opts.get<double>("max_time"),
        opts.get<bool>("diversify"),
        opts.get<int>("samples"),
        opts.get<double>("max_optimization_time"),
        utils::get_rng_arguments_from_options(opts));
}

void add_options_for_cost_partitioning_heuristic(
    plugins::Feature &feature, const string &description, bool consistent) {
    feature.document_language_support("action costs", "supported");
    feature.document_language_support(
        "conditional effects",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    feature.document_language_support(
        "axioms",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    feature.document_property("admissible", "yes");
    feature.document_property("consistent", consistent ? "yes" : "no");
    feature.document_property("safe", "yes");
    feature.document_property("preferred operators", "no");

    feature.add_list_option<shared_ptr<AbstractionGenerator>>(
        "abstractions",
        "abstraction generators",
        "[projections(hillclimbing(max_time=60)), "
        "projections(systematic(2)), "
        "cartesian()]");
    add_heuristic_options_to_feature(feature, description);
}


shared_ptr<MaxCostPartitioningHeuristic> get_max_cp_heuristic(const plugins::Options &opts, const CPFunction &cp_function) {
    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    unique_ptr<DeadEnds> dead_ends = utils::make_unique_ptr<DeadEnds>();
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"), dead_ends.get());
    vector<CostPartitioningHeuristic> cp_heuristics =
        get_cp_heuristic_collection_generator_from_options(opts)->generate_cost_partitionings(
            task_proxy, abstractions, costs, cp_function);
    return plugins::make_shared_from_arg_tuples<MaxCostPartitioningHeuristic>(
        move(abstractions),
        move(cp_heuristics),
        move(dead_ends),
        get_heuristic_arguments_from_options(opts));
}
}
