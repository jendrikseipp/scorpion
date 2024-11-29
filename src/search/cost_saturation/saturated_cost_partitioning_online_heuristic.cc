#include "saturated_cost_partitioning_online_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "order_generator.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/rng_options.h"
#include "../utils/timer.h"

using namespace std;

namespace cost_saturation {
SaturatedCostPartitioningOnlineHeuristic::SaturatedCostPartitioningOnlineHeuristic(
    const shared_ptr<OrderGenerator> &order_generator,
    Saturator saturator,
    const CPFunction &cp_function,
    Abstractions &&abstractions_,
    unique_ptr<DeadEnds> &&dead_ends_,
    const int interval,
    const double max_time,
    const int max_size_kb,
    const bool debug,
    const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description,
    utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      order_generator(order_generator),
      saturator(saturator),
      cp_function(cp_function),
      abstractions(move(abstractions_)),
      dead_ends(move(dead_ends_)),
      interval(interval),
      max_time(max_time),
      max_size_kb(max_size_kb),
      debug(debug),
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

    assert(!task_proxy.needs_to_convert_ancestor_state(ancestor_state));
    // The conversion is unneeded but it results in an unpacked state, which is faster.
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
        abstraction_functions = extract_abstraction_functions_from_useful_abstractions(
            cp_heuristics, nullptr, abstractions);
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


class SaturatedCostPartitioningOnlineHeuristicFeature
    : public plugins::TypedFeature<Evaluator, SaturatedCostPartitioningOnlineHeuristic> {
public:
    SaturatedCostPartitioningOnlineHeuristicFeature() : TypedFeature("scp_online") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Online saturated cost partitioning");
        document_synopsis(
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
        add_options_for_cost_partitioning_heuristic(*this, "scp_online", consistent);
        add_saturator_option(*this);

        add_option<shared_ptr<OrderGenerator>>(
            "orders",
            "order generator",
            "greedy_orders()");
        add_option<int>(
            "max_size",
            "maximum (estimated) heuristic size in KiB",
            "infinity",
            plugins::Bounds("0", "infinity"));
        add_option<double>(
            "max_time",
            "maximum time in seconds for finding orders",
            "200",
            plugins::Bounds("0", "infinity"));
        add_option<int>(
            "interval",
            "select every i-th evaluated state for online diversification",
            "10K",
            plugins::Bounds("1", "infinity"));
        add_option<bool>(
            "debug",
            "print debug output",
            "false");
        utils::add_rng_options_to_feature(*this);
    }

    virtual shared_ptr<SaturatedCostPartitioningOnlineHeuristic> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        shared_ptr<AbstractTask> task = options.get<shared_ptr<AbstractTask>>("transform");
        unique_ptr<DeadEnds> dead_ends = utils::make_unique_ptr<DeadEnds>();
        Abstractions abstractions = generate_abstractions(
            task,
            options.get_list<shared_ptr<AbstractionGenerator>>("abstractions"),
            dead_ends.get());

        return plugins::make_shared_from_arg_tuples<SaturatedCostPartitioningOnlineHeuristic>(
            options.get<shared_ptr<OrderGenerator>>("orders"),
            options.get<Saturator>("saturator"),
            get_cp_function_from_options(options),
            move(abstractions),
            move(dead_ends),
            options.get<int>("interval"),
            options.get<double>("max_time"),
            options.get<int>("max_size"),
            options.get<bool>("debug"),
            get_heuristic_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<SaturatedCostPartitioningOnlineHeuristicFeature> _plugin;
}
