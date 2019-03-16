#include "max_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "utils.h"

#include "../option_parser.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
static void log_info_about_stored_lookup_tables(
    const Abstractions &abstractions,
    const vector<CostPartitioningHeuristic> &cp_heuristics) {
    int num_abstractions = abstractions.size();

    // Print statistics about the number of lookup tables.
    int num_lookup_tables = num_abstractions * cp_heuristics.size();
    int num_stored_lookup_tables = 0;
    for (const auto &cp_heuristic: cp_heuristics) {
        num_stored_lookup_tables += cp_heuristic.get_num_lookup_tables();
    }
    utils::Log() << "Stored lookup tables: " << num_stored_lookup_tables << "/"
                 << num_lookup_tables << " = "
                 << num_stored_lookup_tables / static_cast<double>(num_lookup_tables)
                 << endl;

    // Print statistics about the number of stored values.
    int num_stored_values = 0;
    for (const auto &cp_heuristic : cp_heuristics) {
        num_stored_values += cp_heuristic.get_num_heuristic_values();
    }
    int num_total_values = 0;
    for (const auto &abstraction : abstractions) {
        num_total_values += abstraction->get_num_states();
    }
    num_total_values *= cp_heuristics.size();
    utils::Log() << "Stored values: " << num_stored_values << "/"
                 << num_total_values << " = "
                 << num_stored_values / static_cast<double>(num_total_values) << endl;
}

static AbstractionFunctions extract_abstraction_functions_from_useful_abstractions(
    const vector<CostPartitioningHeuristic> &cp_heuristics,
    const UnsolvabilityHeuristic &unsolvability_heuristic,
    Abstractions &abstractions) {
    int num_abstractions = abstractions.size();

    // Collect IDs of useful abstractions.
    vector<bool> useful_abstractions(num_abstractions, false);
    unsolvability_heuristic.mark_useful_abstractions(useful_abstractions);
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
    return abstraction_functions;
}

MaxCostPartitioningHeuristic::MaxCostPartitioningHeuristic(
    const options::Options &opts,
    Abstractions abstractions,
    vector<CostPartitioningHeuristic> &&cp_heuristics_,
    UnsolvabilityHeuristic &&unsolvability_heuristic_)
    : Heuristic(opts),
      cp_heuristics(move(cp_heuristics_)),
      unsolvability_heuristic(move(unsolvability_heuristic_)) {
    log_info_about_stored_lookup_tables(abstractions, cp_heuristics);

    // We only need abstraction functions during search and no transition systems.
    abstraction_functions = extract_abstraction_functions_from_useful_abstractions(
        cp_heuristics, unsolvability_heuristic, abstractions);

    int num_abstractions = abstractions.size();
    int num_useful_abstractions = abstraction_functions.size();
    utils::Log() << "Useful abstractions: " << num_useful_abstractions << "/"
                 << num_abstractions << " = "
                 << static_cast<double>(num_useful_abstractions) / num_abstractions
                 << endl;
}

MaxCostPartitioningHeuristic::~MaxCostPartitioningHeuristic() {
    print_statistics();
}

int MaxCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int MaxCostPartitioningHeuristic::compute_heuristic(const State &state) const {
    vector<int> abstract_state_ids = get_abstract_state_ids(
        abstraction_functions, state);
    if (unsolvability_heuristic.is_unsolvable(abstract_state_ids)) {
        return DEAD_END;
    }
    return compute_max_h_with_statistics(cp_heuristics, abstract_state_ids, num_best_order);
}

void MaxCostPartitioningHeuristic::print_statistics() const {
    int num_orders = num_best_order.size();
    int num_probably_superfluous = count(num_best_order.begin(), num_best_order.end(), 0);
    int num_probably_useful = num_orders - num_probably_superfluous;
    cout << "Number of times each order was the best order: "
         << num_best_order << endl;
    cout << "Probably useful orders: " << num_probably_useful << "/" << num_orders
         << " = " << 100. * num_probably_useful / num_orders << "%" << endl;
}

void prepare_parser_for_cost_partitioning_heuristic(options::OptionParser &parser) {
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
    parser.document_property("consistent", "yes");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    parser.add_list_option<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators",
        "available generators are cartesian() and projections()",
        "[projections(hillclimbing(max_time=60, random_seed=0)),"
        " projections(systematic(2)), cartesian()]");
    Heuristic::add_options_to_parser(parser);
}

shared_ptr<Heuristic> get_max_cp_heuristic(
    options::OptionParser &parser, CPFunction cp_function) {
    prepare_parser_for_cost_partitioning_heuristic(parser);
    add_order_options_to_parser(parser);

    options::Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    g_store_unsolvable_states_once_hacked = opts.get<bool>("store_unsolvable_states_once");

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    TaskProxy task_proxy(*task);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    Abstractions abstractions = generate_abstractions(
        task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"));
    UnsolvabilityHeuristic unsolvability_heuristic(abstractions);
    vector<CostPartitioningHeuristic> cp_heuristics =
        get_cp_heuristic_collection_generator_from_options(opts).generate_cost_partitionings(
            task_proxy, abstractions, costs, cp_function, unsolvability_heuristic);
    return make_shared<MaxCostPartitioningHeuristic>(
        opts,
        move(abstractions),
        move(cp_heuristics),
        move(unsolvability_heuristic));
}

void add_order_options_to_parser(OptionParser &parser) {
    parser.add_option<shared_ptr<OrderGenerator>>(
        "orders",
        "order generator",
        "greedy_orders()");
    parser.add_option<int>(
        "max_orders",
        "maximum number of orders",
        "infinity",
        Bounds("0", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time for finding orders",
        "200.0",
        Bounds("0", "infinity"));
    parser.add_option<bool>(
        "diversify",
        "only keep orders that have a higher heuristic value than all previous"
        " orders for any of the samples",
        "true");
    parser.add_option<int>(
        "samples",
        "number of samples for diversification",
        "1000",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_optimization_time",
        "maximum time for optimizing each order with hill climbing",
        "2.0",
        Bounds("0.0", "infinity"));
    parser.add_option<bool>(
        "store_unsolvable_states_once",
        "store unsolvable states once per abstraction, instead of once per order. "
        "If store_unsolvable_states_once=true, we store unsolvable states in "
        "UnsolvabilityHeuristic. If store_unsolvable_states_once=false, we "
        "additionally store them in the lookup tables. In any case, we use "
        "UnsolvabilityHeuristic to detect unsolvable states. "
        "(this option only affects the saturated_cost_partitioning() plugin)",
        "true");
    utils::add_rng_options(parser);
}

CostPartitioningHeuristicCollectionGenerator get_cp_heuristic_collection_generator_from_options(
    const options::Options &opts) {
    return CostPartitioningHeuristicCollectionGenerator(
        opts.get<shared_ptr<OrderGenerator>>("orders"),
        opts.get<int>("max_orders"),
        opts.get<double>("max_time"),
        opts.get<bool>("diversify"),
        opts.get<int>("samples"),
        opts.get<double>("max_optimization_time"),
        utils::parse_rng_from_options(opts));
}
}
