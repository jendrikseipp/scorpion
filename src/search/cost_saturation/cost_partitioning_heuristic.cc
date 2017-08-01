#include "cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../utils/logging.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

using namespace std;

namespace cost_saturation {
class AbstractionGenerator;
class CostPartitioningGenerator;

CostPartitioningHeuristic::CostPartitioningHeuristic(const Options &opts)
    : Heuristic(opts),
      rng(utils::parse_rng_from_options(opts)),
      debug(opts.get<bool>("debug")) {
    for (const shared_ptr<AbstractionGenerator> &generator :
         opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators")) {
        int abstractions_before = abstractions.size();
        for (auto &abstraction : generator->generate_abstractions(task)) {
            abstractions.push_back(move(abstraction));
        }
        abstractions_per_generator.push_back(abstractions.size() - abstractions_before);
    }
    cout << "Abstractions: " << abstractions.size() << endl;
    cout << "Abstractions per generator: " << abstractions_per_generator << endl;

    if (debug) {
        for (const unique_ptr<Abstraction> &abstraction : abstractions) {
            abstraction->dump();
        }
    }
}

CostPartitioningHeuristic::~CostPartitioningHeuristic() {
}

int CostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int CostPartitioningHeuristic::compute_heuristic(const State &state) {
    vector<int> local_state_ids = get_local_state_ids(abstractions, state);
    int max_h = compute_max_h_with_statistics(local_state_ids);
    if (max_h == INF) {
        return DEAD_END;
    }
    return max_h;
}

int CostPartitioningHeuristic::compute_max_h_with_statistics(
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

void CostPartitioningHeuristic::print_statistics() const {
    int num_orders = num_best_order.size();
    int num_probably_superfluous = count(num_best_order.begin(), num_best_order.end(), 0);
    int num_probably_useful = num_orders - num_probably_superfluous;
    cout << "Number of times each order was the best order: "
         << num_best_order << endl;
    cout << "Probably useful orders: " << num_probably_useful << "/" << num_orders
         << " = " << 100. * num_probably_useful / num_orders << "%" << endl;
}

void prepare_parser_for_cost_partitioning_heuristic(
    options::OptionParser &parser) {
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
    parser.add_option<shared_ptr<CostPartitioningGenerator>>(
        "orders",
        "cost partitioning generator");
    parser.add_option<bool>(
        "debug",
        "print debugging information",
        "false");
    utils::add_rng_options(parser);
    Heuristic::add_options_to_parser(parser);
}
}
