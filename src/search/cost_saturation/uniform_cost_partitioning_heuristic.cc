#include "uniform_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../tasks/modified_operator_costs_task.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/math.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
static const int COST_FACTOR = 1000;

static vector<int> divide_costs_among_remaining_abstractions(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order, const vector<int> &remaining_costs,
    int position_in_order, bool debug) {
    assert(abstractions.size() == order.size());

    vector<int> op_usages(remaining_costs.size(), 0);
    for (size_t i = position_in_order; i < order.size(); ++i) {
        const Abstraction &abstraction = *abstractions[order[i]];
        for (size_t op_id = 0; op_id < remaining_costs.size(); ++op_id) {
            if (abstraction.operator_is_active(op_id)) {
                ++op_usages[op_id];
            }
        }
    }
    if (debug) {
        cout << "Active operator counts: ";
        print_indexed_vector(op_usages);
    }

    vector<int> divided_costs;
    divided_costs.reserve(remaining_costs.size());
    for (size_t op_id = 0; op_id < remaining_costs.size(); ++op_id) {
        int usages = op_usages[op_id];
        if (remaining_costs[op_id] == INF) {
            divided_costs.push_back(INF);
        } else if (usages == 0) {
            /* Operator is inactive in subsequent abstractions so we can give it
               arbitrary costs. */
            divided_costs.push_back(INF);
        } else {
            divided_costs.push_back(remaining_costs[op_id] / usages);
        }
    }
    if (debug) {
        cout << "Uniformly distributed costs: ";
        print_indexed_vector(divided_costs);
    }
    return divided_costs;
}

static CostPartitioningHeuristic compute_uniform_cost_partitioning(
    const Abstractions &abstractions, const vector<int> &costs, bool debug) {
    vector<int> divided_costs = divide_costs_among_remaining_abstractions(
        abstractions, get_default_order(abstractions.size()), costs, 0, debug);

    CostPartitioningHeuristic cp_heuristic;
    for (size_t i = 0; i < abstractions.size(); ++i) {
        vector<int> h_values =
            abstractions[i]->compute_goal_distances(divided_costs);
        cp_heuristic.add_h_values(i, move(h_values));
    }
    return cp_heuristic;
}

static CostPartitioningHeuristic
compute_opportunistic_uniform_cost_partitioning(
    const Abstractions &abstractions, const vector<int> &order,
    vector<int> &remaining_costs, bool debug) {
    assert(abstractions.size() == order.size());

    if (debug) {
        cout << "remaining costs: ";
        print_indexed_vector(remaining_costs);
    }

    vector<int> divided_costs = divide_costs_among_remaining_abstractions(
        abstractions, order, remaining_costs, 0, debug);

    CostPartitioningHeuristic cp_heuristic;
    for (size_t pos = 0; pos < order.size(); ++pos) {
        int abstraction_id = order[pos];
        const Abstraction &abstraction = *abstractions[abstraction_id];
        divided_costs = divide_costs_among_remaining_abstractions(
            abstractions, order, remaining_costs, pos, debug);
        vector<int> h_values =
            abstraction.compute_goal_distances(divided_costs);
        vector<int> saturated_costs =
            abstraction.compute_saturated_costs(h_values);
        if (debug) {
            cout << "h values: ";
            print_indexed_vector(h_values);
            cout << "saturated costs: ";
            print_indexed_vector(saturated_costs);
        }
        cp_heuristic.add_h_values(abstraction_id, move(h_values));
        reduce_costs(remaining_costs, saturated_costs);
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return cp_heuristic;
}

ScaledCostPartitioningHeuristic::ScaledCostPartitioningHeuristic(
    const shared_ptr<AbstractTask> &task, bool cache_estimates,
    const string &description, utils::Verbosity verbosity)
    : MaxCostPartitioningHeuristic(
          get_scaled_costs_task(task), cache_estimates, description,
          verbosity) {
}

int ScaledCostPartitioningHeuristic::compute_heuristic(
    const State &ancestor_state) {
    int result =
        MaxCostPartitioningHeuristic::compute_heuristic(ancestor_state);
    if (result == DEAD_END) {
        return DEAD_END;
    }
    double epsilon = 0.01;
    return static_cast<int>(
        ceil((result / static_cast<double>(COST_FACTOR)) - epsilon));
}

shared_ptr<AbstractTask> get_scaled_costs_task(
    const shared_ptr<AbstractTask> &task) {
    vector<int> costs = task_properties::get_operator_costs(TaskProxy(*task));
    for (int &cost : costs) {
        if (!utils::is_product_within_limit(cost, COST_FACTOR, INF)) {
            cerr << "Overflowing cost : " << cost << endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
        cost *= COST_FACTOR;
    }
    return make_shared<extra_tasks::ModifiedOperatorCostsTask>(
        task, move(costs));
}

UniformCostPartitioningHeuristic::UniformCostPartitioningHeuristic(
    const shared_ptr<AbstractTask> &task,
    const vector<shared_ptr<AbstractionGenerator>> &abstraction_generators,
    bool opportunistic, bool debug,
    const shared_ptr<OrderGenerator> &order_generator, int max_orders,
    int max_size_kb, double max_time, bool diversify, int num_samples,
    double max_optimization_time, int random_seed, bool cache_estimates,
    const string &description, utils::Verbosity verbosity)
    : ScaledCostPartitioningHeuristic(
          task, cache_estimates, description, verbosity) {
    /* Our base class replaced the task by a copy with scaled costs, so we use
       the scaled task (this->task) for all computations below. */
    unique_ptr<DeadEnds> dead_ends = make_unique<DeadEnds>();
    Abstractions abstractions = generate_abstractions(
        this->task, abstraction_generators, dead_ends.get());
    vector<int> costs = task_properties::get_operator_costs(task_proxy);

    CPHeuristics cp_heuristics;
    if (opportunistic) {
        cp_heuristics = compute_cp_heuristics(
            task_proxy, abstractions, costs,
            [debug](
                const Abstractions &abstractions_, const vector<int> &order,
                vector<int> &remaining_costs, const vector<int> &) {
                return compute_opportunistic_uniform_cost_partitioning(
                    abstractions_, order, remaining_costs, debug);
            },
            order_generator, max_orders, max_size_kb, max_time, diversify,
            num_samples, max_optimization_time, random_seed);
    } else {
        cp_heuristics.push_back(
            compute_uniform_cost_partitioning(abstractions, costs, debug));
    }
    set_cost_partitionings(
        move(abstractions), move(cp_heuristics), move(dead_ends));
}

class UniformCostPartitioningHeuristicFeature
    : public plugins::TypedFeature<TaskIndependentEvaluator> {
public:
    UniformCostPartitioningHeuristicFeature() : TypedFeature("ucp") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("(Opportunistic) uniform cost partitioning");
        document_synopsis(utils::format_conference_reference(
            {"Jendrik Seipp", "Thomas Keller", "Malte Helmert"},
            "A Comparison of Cost Partitioning Algorithms for Optimal Classical Planning",
            "https://jendrikseipp.com/papers/seipp-et-al-icaps2017.pdf",
            "Proceedings of the Twenty-Seventh International Conference on "
            "Automated Planning and Scheduling (ICAPS 2017)",
            "259-268", "AAAI Press", "2017"));

        add_options_for_cost_partitioning_heuristic(*this, "ucp");
        add_order_options(*this);
        add_option<bool>(
            "opportunistic",
            "recalculate uniform cost partitioning after each considered abstraction",
            "false");
        add_option<bool>("debug", "print debugging messages", "false");
    }

    virtual shared_ptr<TaskIndependentEvaluator> create_component(
        const plugins::Options &options) const override {
        return components::make_auto_task_independent_component<
            UniformCostPartitioningHeuristic, Evaluator>(
            get_abstraction_generator_list_from_options(options),
            options.get<bool>("opportunistic"), options.get<bool>("debug"),
            get_order_arguments_from_options(options),
            get_heuristic_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<UniformCostPartitioningHeuristicFeature> _plugin;
}
