#include "uniform_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/logging.h"
#include "../utils/math.h"

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

static vector<vector<int>> compute_uniform_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs,
    bool dynamic,
    bool debug) {
    assert(abstractions.size() == order.size());

    vector<int> remaining_costs = costs;
    if (debug) {
        cout << "remaining costs: ";
        print_indexed_vector(remaining_costs);
    }

    vector<int> divided_costs = compute_divided_costs(
        abstractions, order, remaining_costs, 0, debug);

    vector<vector<int>> h_values_by_abstraction(abstractions.size());
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
        h_values_by_abstraction[abstraction_id] = move(h_values);
        if (dynamic) {
            reduce_costs(remaining_costs, saturated_costs);
        }
        if (dynamic && debug) {
            cout << "remaining costs: ";
            print_indexed_vector(remaining_costs);
        }
    }
    return h_values_by_abstraction;
}

UniformCostPartitioningHeuristic::UniformCostPartitioningHeuristic(const Options &opts)
    : CostPartitioningHeuristic(opts) {
    utils::Timer timer;
    vector<int> costs = get_operator_costs(task_proxy);
    for (int &cost : costs) {
        if (!utils::is_product_within_limit(cost, COST_FACTOR, INF)) {
            cerr << "Overflowing cost : " << cost << endl;
            utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
        }
        cost *= COST_FACTOR;
    }

    vector<int> random_order = get_default_order(abstractions.size());
    rng->shuffle(random_order);
    cout << "Order: " << random_order << endl;
    h_values_by_order = {
        compute_uniform_cost_partitioning(
            abstractions, random_order, costs, opts.get<bool>("dynamic"), debug)};

    for (auto &abstraction : abstractions) {
        abstraction->release_transition_system_memory();
    }

    cout << "Time for computing cost partitionings: " << timer << endl;
    cout << "Orders: " << h_values_by_order.size() << endl;
}

int UniformCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    int result = CostPartitioningHeuristic::compute_heuristic(global_state);
    if (result == DEAD_END) {
        return DEAD_END;
    }
    double epsilon = 0.01;
    return ceil((result / static_cast<double>(COST_FACTOR)) - epsilon);
}


static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Uniform cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    parser.add_option<bool>(
        "dynamic",
        "recalculate costs after each considered abstraction",
        "false");

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return new UniformCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("uniform_cost_partitioning", _parse);
}
