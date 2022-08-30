#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "order_generator.h"
#include "saturated_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../algorithms/partial_state_tree.h"
#include "../task_utils/task_properties.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/rng_options.h"
#include "../utils/timer.h"

using namespace std;

namespace cost_saturation {
// TODO: avoid code duplication
static void extract_useful_abstraction_functions(
    const vector<CostPartitioningHeuristic> &cp_heuristics,
    Abstractions &abstractions,
    AbstractionFunctions &abstraction_functions) {
    int num_abstractions = abstractions.size();

    // Collect IDs of useful abstractions.
    vector<bool> useful_abstractions(num_abstractions, false);
    for (const auto &cp_heuristic : cp_heuristics) {
        cp_heuristic.mark_useful_abstractions(useful_abstractions);
    }

    abstraction_functions.reserve(num_abstractions);
    for (int i = 0; i < num_abstractions; ++i) {
        if (useful_abstractions[i]) {
            abstraction_functions.push_back(abstractions[i]->extract_abstraction_function());
        } else {
            abstraction_functions.push_back(nullptr);
        }
    }
    assert(abstraction_functions.size() == abstractions.size());
}


SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(
    const options::Options &opts,
    Abstractions &&abstractions_,
    unique_ptr<DeadEnds> &&dead_ends_)
    : Heuristic(opts),
      order_generator(opts.get<shared_ptr<OrderGenerator>>("orders")),
      saturator(opts.get<Saturator>("saturator")),
      cp_function(get_cp_function_from_options(opts)),
      abstractions(move(abstractions_)),
      dead_ends(move(dead_ends_)),
      interval(opts.get<int>("interval")),
      max_time(opts.get<double>("max_time")),
      max_size_kb(opts.get<int>("max_size")),
      debug(opts.get<bool>("debug")),
      costs(task_properties::get_operator_costs(task_proxy)),
      improve_heuristic(true),
      size_kb(0),
      num_evaluated_states(0),
      num_scps_computed(0) {
    order_generator->initialize(abstractions, costs);
    for (const auto &cp : cp_heuristics) {
        size_kb += cp.estimate_size_in_kb();
    }
    improve_heuristic_timer = utils::make_unique_ptr<utils::Timer>(false);
    select_state_timer = utils::make_unique_ptr<utils::Timer>(false);
}

SaturatedCostPartitioningOnlineHeuristic::~SaturatedCostPartitioningOnlineHeuristic() {
    if (improve_heuristic) {
        print_intermediate_statistics();
        print_final_statistics();
    }
}

int SaturatedCostPartitioningOnlineHeuristic::compute_heuristic(const State &ancestor_state) {
    if (improve_heuristic) {
        improve_heuristic_timer->resume();
    }

    State state = convert_ancestor_state(ancestor_state);

    if (dead_ends && dead_ends->subsumes(state)) {
        improve_heuristic_timer->stop();
        return DEAD_END;
    }

    vector<int> abstract_state_ids;
    if (improve_heuristic) {
        assert(!abstractions.empty() && abstraction_functions.empty());
        abstract_state_ids = get_abstract_state_ids(abstractions, state);
    } else {
        assert(abstractions.empty() && !abstraction_functions.empty());
        abstract_state_ids = get_abstract_state_ids(abstraction_functions, state);
    }

    int max_h = compute_max_h(cp_heuristics, abstract_state_ids);
    if (max_h == INF) {
        improve_heuristic_timer->stop();
        return DEAD_END;
    }

    if (improve_heuristic &&
        ((*improve_heuristic_timer)() >= max_time || size_kb >= max_size_kb)) {
        utils::g_log << "Stop heuristic improvement phase." << endl;
        improve_heuristic = false;
        extract_useful_abstraction_functions(
            cp_heuristics, abstractions, abstraction_functions);
        utils::release_vector_memory(abstractions);
        print_intermediate_statistics();
        print_final_statistics();
    }
    bool stored_scp = false;
    if (improve_heuristic && (num_evaluated_states % interval == 0)) {
        if (debug) {
            utils::g_log << "Compute SCP for " << ancestor_state.get_id() << endl;
        }
        Order order = order_generator->compute_order_for_state(
            abstract_state_ids, num_evaluated_states == 0);

        CostPartitioningHeuristic cost_partitioning;
        vector<int> remaining_costs = costs;
        if (saturator == Saturator::PERIMSTAR) {
            // Compute only the first SCP here, and the second below if necessary.
            cost_partitioning = compute_perim_saturated_cost_partitioning(
                abstractions, order, remaining_costs, abstract_state_ids);
        } else {
            cost_partitioning = cp_function(abstractions, order, remaining_costs, abstract_state_ids);
        }
        ++num_scps_computed;

        int new_h = cost_partitioning.compute_heuristic(abstract_state_ids);

        if (new_h > max_h) {
            /* Adding the second SCP is only useful if the order is already diverse
               for the current state. */
            if (saturator == Saturator::PERIMSTAR) {
                cost_partitioning.add(
                    compute_saturated_cost_partitioning(
                        abstractions, order, remaining_costs, abstract_state_ids));
            }
            size_kb += cost_partitioning.estimate_size_in_kb();
            cp_heuristics.push_back(move(cost_partitioning));
            stored_scp = true;
        }
        max_h = max(max_h, new_h);
    }

    ++num_evaluated_states;
    if (stored_scp) {
        print_intermediate_statistics();
    }
    improve_heuristic_timer->stop();
    return max_h;
}

void SaturatedCostPartitioningOnlineHeuristic::print_intermediate_statistics() const {
    utils::g_log << "Evaluated states: " << num_evaluated_states
                 << ", selected states: " << num_scps_computed
                 << ", stored SCPs: " << cp_heuristics.size()
                 << ", heuristic size: " << size_kb << " KB"
                 << ", selection time: " << *select_state_timer
                 << ", diversification time: " << *improve_heuristic_timer
                 << endl;
}

void SaturatedCostPartitioningOnlineHeuristic::print_final_statistics() const {
    // Print the number of stored lookup tables.
    int num_stored_lookup_tables = 0;
    for (const auto &cp_heuristic: cp_heuristics) {
        num_stored_lookup_tables += cp_heuristic.get_num_lookup_tables();
    }
    utils::g_log << "Stored lookup tables: " << num_stored_lookup_tables << endl;

    // Print the number of stored values.
    int num_stored_values = 0;
    for (const auto &cp_heuristic : cp_heuristics) {
        num_stored_values += cp_heuristic.get_num_heuristic_values();
    }
    utils::g_log << "Stored values: " << num_stored_values << endl;

    utils::g_log << "Evaluated states: " << num_evaluated_states << endl;
    utils::g_log << "Time for improving heuristic: " << *improve_heuristic_timer << endl;
    utils::g_log << "Estimated heuristic size: " << size_kb << " KB" << endl;
    utils::g_log << "Computed SCPs: " << num_scps_computed << endl;
    utils::g_log << "Stored SCPs: " << cp_heuristics.size() << endl;
}


static shared_ptr<Heuristic> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Online saturated cost partitioning",
        "Compute the maximum over multiple saturated cost partitioning heuristics "
        "diversified during the search. For details, see " +
        utils::format_conference_reference(
            {"Jendrik Seipp"},
            "Online Saturated Cost Partitioning for Classical Planning",
            "https://ai.dmi.unibas.ch/papers/seipp-icaps2021.pdf",
            "Proceedings of the 31st International Conference on Automated "
            "Planning and Scheduling (ICAPS 2021)",
            "317-321",
            "AAAI Press",
            "2021"));

    // The online version is not consistent.
    bool consistent = false;
    prepare_parser_for_cost_partitioning_heuristic(parser, consistent);
    add_saturator_option(parser);

    parser.add_option<shared_ptr<OrderGenerator>>(
        "orders",
        "order generator",
        "greedy_orders()");
    parser.add_option<int>(
        "max_size",
        "maximum (estimated) heuristic size in KiB",
        "infinity",
        Bounds("0", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for finding orders",
        "200",
        Bounds("0", "infinity"));
    parser.add_option<int>(
        "interval",
        "select every i-th evaluated state for online diversification",
        "10K",
        Bounds("1", "infinity"));
    parser.add_option<bool>(
        "debug",
        "print debug output",
        "false");
    utils::add_rng_options(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> task = opts.get<shared_ptr<AbstractTask>>("transform");
    unique_ptr<DeadEnds> dead_ends = utils::make_unique_ptr<DeadEnds>();
    Abstractions abstractions = generate_abstractions(
        task,
        opts.get_list<shared_ptr<AbstractionGenerator>>("abstractions"),
        dead_ends.get());

    return make_shared<SaturatedCostPartitioningOnlineHeuristic>(
        opts, move(abstractions), move(dead_ends));
}

static Plugin<Evaluator> _plugin("scp_online", _parse, "heuristics_cost_partitioning");
}
