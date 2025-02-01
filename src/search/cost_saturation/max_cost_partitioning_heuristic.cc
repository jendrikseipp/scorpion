#include "max_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../utils/logging.h"

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
    utils::g_log << "Stored lookup tables: " << num_stored_lookup_tables << "/"
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
    utils::g_log << "Stored values: " << num_stored_values << "/"
                 << num_total_values << " = "
                 << num_stored_values / static_cast<double>(num_total_values) << endl;
}

MaxCostPartitioningHeuristic::MaxCostPartitioningHeuristic(
    Abstractions &&abstractions,
    vector<CostPartitioningHeuristic> &&cp_heuristics_,
    unique_ptr<DeadEnds> &&dead_ends_,
    const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description, utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      cp_heuristics(move(cp_heuristics_)),
      dead_ends(move(dead_ends_)),
      unsolvability_heuristic(abstractions, cp_heuristics) {
    log_info_about_stored_lookup_tables(abstractions, cp_heuristics);

    // We only need abstraction functions during search and no transition systems.
    abstraction_functions = extract_abstraction_functions_from_useful_abstractions(
        cp_heuristics, &unsolvability_heuristic, abstractions);
}

MaxCostPartitioningHeuristic::~MaxCostPartitioningHeuristic() {
    print_statistics();
}

int MaxCostPartitioningHeuristic::compute_heuristic(const State &ancestor_state) {
    assert(!task_proxy.needs_to_convert_ancestor_state(ancestor_state));
    // The conversion is unneeded but it results in an unpacked state, which is faster.
    State state = convert_ancestor_state(ancestor_state);
    if (dead_ends && dead_ends->subsumes(state)) {
        return DEAD_END;
    }
    vector<int> abstract_state_ids = get_abstract_state_ids(
        abstraction_functions, state);
    if (unsolvability_heuristic.is_unsolvable(abstract_state_ids)) {
        return DEAD_END;
    }
    return compute_max_h(cp_heuristics, abstract_state_ids, &num_best_order);
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
}
