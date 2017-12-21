#include "uniform_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioned_heuristic.h"
#include "cost_partitioning_collection_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"
#include "../tasks/modified_operator_costs_task.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
// Multiply all costs by this factor to avoid using real-values costs.
static const int COST_FACTOR = 1000;

static vector<int> compute_divided_costs(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &remaining_costs,
    int pos,
    bool debug) {
    assert(abstractions.size() == order.size());
    assert(utils::in_bounds(pos, order));

    vector<int> op_usages(remaining_costs.size(), 0);
    for (size_t i = pos; i < order.size(); ++i) {
        const Abstraction &abstraction = *abstractions[order[i]];
        for (int op_id : abstraction.get_active_operators()) {
            ++op_usages[op_id];
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
            /* Operator is inactive in subsequent abstractions so we
               can use an arbitrary value here. */
            divided_costs.push_back(-1);
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

static CostPartitionedHeuristic compute_uniform_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool dynamic,
    bool sparse,
    bool debug) {
    assert(abstractions.size() == order.size());

    vector<int> remaining_costs = costs;
    if (debug) {
        cout << "remaining costs: ";
        print_indexed_vector(remaining_costs);
    }

    vector<int> divided_costs = compute_divided_costs(
        abstractions, order, remaining_costs, 0, debug);

    CostPartitionedHeuristic cp_heuristic;
    for (size_t pos = 0; pos < order.size(); ++pos) {
        int abstraction_id = order[pos];
        Abstraction &abstraction = *abstractions[abstraction_id];
        if (dynamic) {
            divided_costs = compute_divided_costs(
                abstractions, order, remaining_costs, pos, debug);
        }
        auto pair = abstraction.compute_goal_distances_and_saturated_costs(
            divided_costs);
        vector<int> &h_values = pair.first;
        vector<int> &saturated_costs = pair.second;
        if (debug) {
            cout << "h-values: ";
            print_indexed_vector(h_values);
            cout << "saturated costs: ";
            print_indexed_vector(saturated_costs);
        }
        cp_heuristic.add_cp_heuristic_values(
            abstraction_id, move(h_values), sparse);
        if (dynamic) {
            reduce_costs(remaining_costs, saturated_costs);
        }
        if (dynamic && debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return cp_heuristic;
}

UniformCostPartitioningHeuristic::UniformCostPartitioningHeuristic(
    const Options &opts, Abstractions &&abstractions, CPHeuristics &&cp_heuristics)
    : CostPartitioningHeuristic(opts, move(abstractions), move(cp_heuristics)) {
}

int UniformCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    int result = CostPartitioningHeuristic::compute_heuristic(global_state);
    if (result == DEAD_END) {
        return DEAD_END;
    }
    double epsilon = 0.01;
    return ceil((result / static_cast<double>(COST_FACTOR)) - epsilon);
}


static shared_ptr<AbstractTask> get_scaled_costs_task(
    const shared_ptr<AbstractTask> &task) {
    vector<int> costs = task_properties::get_operator_costs(TaskProxy(*task));
    for (int &cost : costs) {
        if (!utils::is_product_within_limit(cost, COST_FACTOR, INF)) {
            cerr << "Overflowing cost : " << cost << endl;
            utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
        }
        cost *= COST_FACTOR;
    }
    return make_shared<extra_tasks::ModifiedOperatorCostsTask>(task, move(costs));
}


static CostPartitionedHeuristic get_ucp_heuristic(
    const TaskProxy &task_proxy, const Abstractions &abstractions, bool debug) {
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    vector<int> order = get_default_order(abstractions.size());
    bool sparse = true;
    return compute_uniform_cost_partitioning(
        abstractions, order, costs, false, sparse, debug);
}

static CPHeuristics get_oucp_heuristics(
    const TaskProxy &task_proxy,
    const Abstractions &abstractions,
    const CostPartitioningCollectionGenerator &cps_generator,
    bool debug) {
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    return cps_generator.get_cost_partitionings(
        task_proxy, abstractions, costs,
        [debug](
            const Abstractions &abstractions,
            const vector<int> &order,
            const vector<int> &costs,
            bool sparse) {
            return compute_uniform_cost_partitioning(
                abstractions, order, costs, true, sparse, debug);
        });
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Uniform cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_cost_partitioning_collection_options_to_parser(parser);
    parser.add_option<bool>(
        "dynamic",
        "recalculate costs after each considered abstraction",
        "false");

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> scaled_costs_task =
        get_scaled_costs_task(opts.get<shared_ptr<AbstractTask>>("transform"));
    opts.set<shared_ptr<AbstractTask>>("transform", scaled_costs_task);

    Abstractions abstractions = generate_abstractions(
        scaled_costs_task,
        opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"));

    TaskProxy scaled_costs_task_proxy(*scaled_costs_task);
    bool debug = opts.get<bool>("debug");

    CPHeuristics cp_heuristics;
    if (opts.get<bool>("dynamic")) {
        cp_heuristics = get_oucp_heuristics(
            scaled_costs_task_proxy,
            abstractions,
            get_cp_collection_generator_from_options(opts),
            debug);
    } else {
        cp_heuristics.push_back(get_ucp_heuristic(
                                    scaled_costs_task_proxy, abstractions, debug));
    }

    return new UniformCostPartitioningHeuristic(opts, move(abstractions), move(cp_heuristics));
}

static Plugin<Heuristic> _plugin("uniform_cost_partitioning", _parse);
}
