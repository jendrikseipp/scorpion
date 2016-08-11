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

static AdditiveCartesianHeuristic *create_additive_cartesian_heuristic(
    vector<unique_ptr<Abstraction>> &abstractions,
    const Options &opts) {
    shared_ptr<AbstractTask> task(get_task_from_options(opts));

    vector<int> remaining_costs;
    for (OperatorProxy op : TaskProxy(*task).get_operators())
        remaining_costs.push_back(op.get_cost());

    vector<CartesianHeuristicFunction> heuristic_functions;
    for (unique_ptr<Abstraction> &abstraction : abstractions) {
        abstraction->set_operator_costs(remaining_costs);
        heuristic_functions.emplace_back(
            abstraction->get_task(),
            abstraction->get_refinement_hierarchy(),
            abstraction->compute_h_map());
        reduce_costs(remaining_costs, abstraction->get_saturated_costs());
    }
    return new AdditiveCartesianHeuristic(opts, move(heuristic_functions));
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
    } else if (cost_partitioning_type == CostPartitioningType::SATURATED_POSTHOC) {
        int num_orders = opts.get<int>("orders");
        int num_samples = opts.get<int>("samples");
        vector<unique_ptr<Abstraction>> abstractions =
            cost_saturation.extract_abstractions();
        // TODO: add RefinementHierarchy::get_task().
        vector<shared_ptr<AbstractTask>> subtasks;
        subtasks.reserve(abstractions.size());
        vector<shared_ptr<RefinementHierarchy>> refinement_hierarchies;
        refinement_hierarchies.reserve(abstractions.size());
        for (unique_ptr<Abstraction> &abstraction : abstractions) {
            subtasks.push_back(abstraction->get_task());
            refinement_hierarchies.push_back(abstraction->get_refinement_hierarchy());
        }
        vector<int> operator_costs = get_operator_costs(TaskProxy(*task));
        vector<vector<vector<int>>> h_values_by_orders;
        if (num_samples == 0) {
            h_values_by_orders = compute_saturated_cost_partitionings(
                abstractions,
                operator_costs,
                num_orders);
        } else {
            // TODO: add samples.
            vector<State> samples = {TaskProxy(*task).get_initial_state()};
            SCPOptimizer scp_optimizer(
                move(abstractions), subtasks, refinement_hierarchies, operator_costs, samples);
            for (int i = 0; i < num_orders; ++i) {
                //h_values_by_orders.push_back(scp_optimizer.extract_order());
            }
        }
        return new MaxCartesianHeuristic(
            heuristic_opts,
            move(subtasks),
            move(refinement_hierarchies),
            move(h_values_by_orders));
    } else if (cost_partitioning_type == CostPartitioningType::SATURATED_MAX) {
        vector<unique_ptr<Abstraction>> abstractions =
            cost_saturation.extract_abstractions();
        sort(abstractions.begin(), abstractions.end());
        vector<ScalarEvaluator *> additive_heuristics;
        do {
            additive_heuristics.push_back(
                create_additive_cartesian_heuristic(abstractions, heuristic_opts));
        } while (next_permutation(abstractions.begin(), abstractions.end()));

        Options max_evaluator_opts;
        max_evaluator_opts.set("evals", additive_heuristics);
        return new max_evaluator::MaxEvaluator(max_evaluator_opts);
    } else {
        ABORT("Invalid cost partitioning type");
    }
}

static Plugin<ScalarEvaluator> _plugin("cegar", _parse);
}
