#include "uniform_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "scp_generator.h"
#include "types.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_tools.h"

#include "../utils/logging.h"
#include "../utils/math.h"

using namespace std;

namespace cost_saturation {
static const int COST_FACTOR = 1000;

static vector<int> compute_divided_costs(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &remaining_costs,
    int pos) {
    assert(abstractions.size() == order.size());
    assert(utils::in_bounds(pos, order));
    const bool debug = false;

    vector<int> op_usages(remaining_costs.size(), 0);
    for (size_t i = pos; i < order.size(); ++i) {
        const Abstraction &abstraction = *abstractions[order[i]];
        if (debug) {
            abstraction.dump();
        }
        for (int op_id : abstraction.get_active_operators()) {
            ++op_usages[op_id];
        }
    }
    cout << "Active operator counts: " << op_usages << endl;

    vector<int> divided_costs;
    divided_costs.reserve(remaining_costs.size());
    for (size_t op_id = 0; op_id < remaining_costs.size(); ++op_id) {
        int usages = op_usages[op_id];
        int divided_cost = usages ? remaining_costs[op_id] / usages : -1;
        divided_costs.push_back(divided_cost);
    }
    cout << "Uniformly distributed costs: " << divided_costs << endl;
    return divided_costs;
}

static vector<vector<int>> compute_uniform_cost_partitioning(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> &order,
    const vector<int> &costs) {
    assert(abstractions.size() == order.size());
    const bool use_cost_saturation = false;
    const bool debug = false;

    vector<int> divided_costs = compute_divided_costs(abstractions, order, costs, 0);

    vector<vector<int>> h_values_by_abstraction(abstractions.size());
    for (int pos : order) {
        Abstraction &abstraction = *abstractions[pos];
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
        h_values_by_abstraction[pos] = move(h_values);
        if (use_cost_saturation) {
            reduce_costs(divided_costs, saturated_costs);
        }
        if (debug) {
            cout << "remaining costs: ";
            print_indexed_vector(divided_costs);
        }
    }
    return h_values_by_abstraction;
}

UniformCostPartitioningHeuristic::UniformCostPartitioningHeuristic(const Options &opts)
    : Heuristic(opts) {
    vector<unique_ptr<Abstraction>> abstractions;
    for (const shared_ptr<AbstractionGenerator> &generator :
         opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators")) {
        for (AbstractionAndStateMap &pair : generator->generate_abstractions(task)) {
            abstractions.push_back(move(pair.first));
            state_maps.push_back(move(pair.second));
        }
    }
    cout << "Abstractions: " << abstractions.size() << endl;

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
    g_rng()->shuffle(random_order);
    h_values_by_order = {
        compute_uniform_cost_partitioning(abstractions, random_order, costs)};
    num_best_order.resize(h_values_by_order.size(), 0);

    cout << "Time for computing cost partitionings: " << timer << endl;
    cout << "Orders: " << h_values_by_order.size() << endl;
}

int UniformCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int UniformCostPartitioningHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(state_maps, state);
    int max_h = compute_max_h_with_statistics(local_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h / COST_FACTOR;
}

int UniformCostPartitioningHeuristic::compute_max_h_with_statistics(
    const vector<int> &local_state_ids) const {
    int max_h = 0;
    int best_id = -1;
    int current_id = 0;
    for (const vector<vector<int>> &h_values_by_abstraction : h_values_by_order) {
        int sum_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        if (sum_h > max_h) {
            max_h = sum_h;
            best_id = current_id;
        }
        if (sum_h == INF) {
            break;
        }
        ++current_id;
    }
    assert(max_h >= 0);

    if (best_id != -1 && !num_best_order.empty()) {
        assert(utils::in_bounds(best_id, num_best_order));
        ++num_best_order[best_id];
    }

    return max_h;
}

void UniformCostPartitioningHeuristic::print_statistics() const {
    int num_superfluous = count(num_best_order.begin(), num_best_order.end(), 0);
    int num_orders = num_best_order.size();
    double percentage_superfluous =
        (num_orders == 0) ? 0 : num_superfluous * 100.0 / num_orders;
    cout << "Number of times each order was the best order: "
         << num_best_order << endl;
    cout << "Superfluous orders: " << num_superfluous << "/" << num_orders
         << " = " << percentage_superfluous << endl;
}

static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Saturated cost partitioning heuristic",
        "");

    parser.document_language_support("action costs", "supported");
    parser.document_language_support(
        "conditional effects",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_language_support(
        "axioms",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_property("admissible", "yes");
    parser.document_property(
        "consistent",
        "yes, if all abstraction generators represent consistent heuristics");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    parser.add_list_option<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators",
        "methods that generate abstractions");
    parser.add_option<shared_ptr<SCPGenerator>>(
        "orders",
        "saturated cost partitioning generator",
        "random(1)");
    Heuristic::add_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    opts.verify_list_non_empty<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators");

    if (parser.dry_run())
        return nullptr;

    return new UniformCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("uniform_cost_partitioning", _parse);
}
