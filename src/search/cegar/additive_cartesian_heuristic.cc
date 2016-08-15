#include "additive_cartesian_heuristic.h"

#include "abstraction.h"
#include "cartesian_heuristic_function.h"
#include "cost_saturation.h"
#include "max_cartesian_heuristic.h"
#include "ocp_heuristic.h"
#include "scp_optimizer.h"
#include "utils.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../sampling.h"
#include "../successor_generator.h"
#include "../task_tools.h"

#include "../evaluators/max_evaluator.h"

#include "../lp/lp_solver.h"

#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/rng.h"

#include <cassert>

using namespace std;

namespace cegar {
static vector<CartesianHeuristicFunction> generate_heuristic_functions(
    const options::Options &opts) {
    g_log << "Initializing additive Cartesian heuristic..." << endl;
    CostSaturation cost_saturation(
        static_cast<CostPartitioningType>(opts.get_enum("cost_partitioning")),
        opts.get_list<shared_ptr<SubtaskGenerator>>("subtasks"),
        opts.get<int>("max_states"),
        opts.get<int>("max_transitions"),
        opts.get<double>("max_time"),
        opts.get<bool>("use_general_costs"),
        static_cast<PickSplit>(opts.get<int>("pick")));
    cost_saturation.initialize(get_task_from_options(opts));
    return cost_saturation.extract_heuristic_functions();
}

AdditiveCartesianHeuristic::AdditiveCartesianHeuristic(
    const options::Options &opts)
    : Heuristic(opts),
      heuristic_functions(generate_heuristic_functions(opts)) {
}

AdditiveCartesianHeuristic::AdditiveCartesianHeuristic(
    const options::Options &opts,
    std::vector<CartesianHeuristicFunction> &&heuristic_functions)
    : Heuristic(opts),
      heuristic_functions(move(heuristic_functions)) {
}

int AdditiveCartesianHeuristic::compute_heuristic(const GlobalState &global_state) {
    State state = convert_global_state(global_state);
    return compute_heuristic(state);
}

int AdditiveCartesianHeuristic::compute_heuristic(const State &state) {
    int sum_h = 0;
    for (const CartesianHeuristicFunction &function : heuristic_functions) {
        int value = function.get_value(state);
        assert(value >= 0);
        if (value == INF)
            return DEAD_END;
        sum_h += value;
    }
    assert(sum_h >= 0);
    return sum_h;
}

static vector<vector<vector<int>>> compute_all_saturated_cost_partitionings(
    const vector<unique_ptr<Abstraction>> &abstractions,
    const vector<int> operator_costs) {
    vector<int> indices = get_default_order(abstractions.size());

    vector<vector<vector<int>>> h_values_by_orders;
    do {
        h_values_by_orders.push_back(
            compute_saturated_cost_partitioning(
                abstractions, indices, operator_costs));
    } while (next_permutation(indices.begin(), indices.end()));
    return h_values_by_orders;
}

static ScalarEvaluator *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Additive CEGAR heuristic",
        "See the paper introducing Counterexample-guided Abstraction "
        "Refinement (CEGAR) for classical planning:" +
        utils::format_paper_reference(
            {"Jendrik Seipp", "Malte Helmert"},
            "Counterexample-guided Cartesian Abstraction Refinement",
            "http://ai.cs.unibas.ch/papers/seipp-helmert-icaps2013.pdf",
            "Proceedings of the 23rd International Conference on Automated "
            "Planning and Scheduling (ICAPS 2013)",
            "347-351",
            "AAAI Press 2013") +
        "and the paper showing how to make the abstractions additive:" +
        utils::format_paper_reference(
            {"Jendrik Seipp", "Malte Helmert"},
            "Diverse and Additive Cartesian Abstraction Heuristics",
            "http://ai.cs.unibas.ch/papers/seipp-helmert-icaps2014.pdf",
            "Proceedings of the 24th International Conference on "
            "Automated Planning and Scheduling (ICAPS 2014)",
            "289-297",
            "AAAI Press 2014"));
    parser.document_language_support("action costs", "supported");
    parser.document_language_support("conditional effects", "not supported");
    parser.document_language_support("axioms", "not supported");
    parser.document_property("admissible", "yes");
    // TODO: Is the additive version consistent as well?
    parser.document_property("consistent", "yes");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    parser.add_list_option<shared_ptr<SubtaskGenerator>>(
        "subtasks",
        "subtask generators",
        "[landmarks(),goals()]");
    parser.add_option<int>(
        "max_states",
        "maximum sum of abstract states over all abstractions",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_transitions",
        "maximum sum of real transitions (excluding self-loops) over "
        " all abstractions",
        "2000000",
        Bounds("0", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for building abstractions",
        "infinity",
        Bounds("0.0", "infinity"));
    /*
      We reserve some memory to be able to recover from out-of-memory
      situations gracefully. When the memory runs out, we stop refining and
      start the next refinement or the search. Due to memory fragmentation
      the memory used for building the abstraction (states, transitions,
      etc.) often can't be reused for things that require big continuous
      blocks of memory. It is for this reason that we require such a large
      amount of memory padding.
    */
    parser.add_option<int>(
        "extra_memory_padding",
        "amount of extra memory in MB to reserve for recovering from"
        " out-of-memory situations gracefully (75 MB work well for CEGAR)",
        "0",
        Bounds("0", "infinity"));
    vector<string> pick_strategies;
    pick_strategies.push_back("RANDOM");
    pick_strategies.push_back("MIN_UNWANTED");
    pick_strategies.push_back("MAX_UNWANTED");
    pick_strategies.push_back("MIN_REFINED");
    pick_strategies.push_back("MAX_REFINED");
    pick_strategies.push_back("MIN_HADD");
    pick_strategies.push_back("MAX_HADD");
    parser.add_enum_option(
        "pick", pick_strategies, "split-selection strategy", "MAX_REFINED");
    vector<string> cp_types;
    cp_types.push_back("SATURATED");
    cp_types.push_back("SATURATED_POSTHOC");
    cp_types.push_back("SATURATED_MAX");
    cp_types.push_back("OPTIMAL");
    cp_types.push_back("OPTIMAL_OPERATOR_COUNTING");
    parser.add_enum_option(
        "cost_partitioning", cp_types, "cost partitioning method", "SATURATED");
    lp::add_lp_solver_option_to_parser(parser);
    parser.add_option<bool>(
        "use_general_costs",
        "allow negative costs in cost partitioning",
        "true");
    parser.add_option<int>(
        "orders",
        "number of abstraction orders to maximize over",
        "1",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "samples",
        "number of sample states to optimize for",
        "0",
        Bounds("0", "infinity"));
    Heuristic::add_options_to_parser(parser);
    Options opts = parser.parse();

    if (parser.dry_run())
        return nullptr;

    shared_ptr<AbstractTask> task(get_task_from_options(opts));
    TaskProxy task_proxy(*task);
    CostPartitioningType cost_partitioning_type =
        static_cast<CostPartitioningType>(opts.get_enum("cost_partitioning"));
    int extra_memory_padding_in_mb = opts.get<int>("extra_memory_padding");

    Options heuristic_opts;
    heuristic_opts.set<shared_ptr<AbstractTask>>(
        "transform", task);
    heuristic_opts.set<int>(
        "cost_type", NORMAL);
    heuristic_opts.set<bool>(
        "cache_estimates", opts.get<bool>("cache_estimates"));

    CostSaturation cost_saturation(
        cost_partitioning_type,
        opts.get_list<shared_ptr<SubtaskGenerator>>("subtasks"),
        opts.get<int>("max_states"),
        opts.get<int>("max_transitions"),
        opts.get<double>("max_time"),
        opts.get<bool>("use_general_costs"),
        static_cast<PickSplit>(opts.get<int>("pick")));
    if (extra_memory_padding_in_mb > 0)
        utils::reserve_extra_memory_padding(extra_memory_padding_in_mb);
    cost_saturation.initialize(task);
    if (utils::extra_memory_padding_is_reserved())
        utils::release_extra_memory_padding();

    if (cost_partitioning_type == CostPartitioningType::SATURATED) {
        return new AdditiveCartesianHeuristic(
            heuristic_opts, cost_saturation.extract_heuristic_functions());
    } else if (cost_partitioning_type == CostPartitioningType::OPTIMAL) {
        heuristic_opts.set<bool>(
            "use_general_costs", opts.get<bool>("use_general_costs"));
        heuristic_opts.set<int>("lpsolver", opts.get_enum("lpsolver"));
        return new OptimalCostPartitioningHeuristic(
            heuristic_opts, cost_saturation.extract_transition_systems());
    } else if (cost_partitioning_type == CostPartitioningType::SATURATED_POSTHOC ||
               cost_partitioning_type ==CostPartitioningType::SATURATED_MAX) {
        int num_orders = opts.get<int>("orders");
        int num_samples = opts.get<int>("samples");

        vector<unique_ptr<Abstraction>> abstractions =
            cost_saturation.extract_abstractions();

        vector<shared_ptr<RefinementHierarchy>> refinement_hierarchies;
        refinement_hierarchies.reserve(abstractions.size());
        for (unique_ptr<Abstraction> &abstraction : abstractions) {
            refinement_hierarchies.push_back(abstraction->get_refinement_hierarchy());
        }

        vector<int> operator_costs = get_operator_costs(task_proxy);

        if (cost_partitioning_type == CostPartitioningType::SATURATED_MAX) {
            vector<vector<vector<int>>> h_values_by_orders =
                compute_all_saturated_cost_partitionings(
                    abstractions, operator_costs);
            return new MaxCartesianHeuristic(
                heuristic_opts,
                move(refinement_hierarchies),
                move(h_values_by_orders));
        }

        vector<vector<vector<int>>> h_values_by_orders;
        vector<vector<int>> h_values_by_abstraction =
            compute_saturated_cost_partitioning(
                abstractions, get_default_order(abstractions.size()), operator_costs);
        State initial_state = task_proxy.get_initial_state();
        vector<int> local_state_ids = get_local_state_ids(
            refinement_hierarchies, initial_state);
        int init_h = compute_sum_h(local_state_ids, h_values_by_abstraction);
        cout << "Initial h value for default order: " << init_h << endl;
        SuccessorGenerator successor_generator(task);
        SCPOptimizer scp_optimizer(
            move(abstractions), refinement_hierarchies, operator_costs);
        function<bool(const State &state)> dead_end_function =
                [&](const State &state) {
            vector<int> local_state_ids = get_local_state_ids(
                refinement_hierarchies, state);
            return compute_sum_h(local_state_ids, h_values_by_abstraction) == INF;
        };
        for (int i = 0; i < num_orders; ++i) {
            vector<State> samples;
            if (init_h != INF) {
                samples = sample_states_with_random_walks(
                    task_proxy,
                    successor_generator,
                    num_samples,
                    init_h,
                    get_average_operator_cost(task_proxy),
                    dead_end_function);
            }
            h_values_by_orders.push_back(scp_optimizer.find_cost_partitioning(samples));
        }
        return new MaxCartesianHeuristic(
            heuristic_opts,
            move(refinement_hierarchies),
            move(h_values_by_orders));
    } else {
        ABORT("Invalid cost partitioning type");
    }
}

static Plugin<ScalarEvaluator> _plugin("cegar", _parse);
}
