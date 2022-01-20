#include "depth_first_search.h"

#include "../evaluator.h"
#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"

#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/timer.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace depth_first_search {
static const int INF = numeric_limits<int>::max();


DepthFirstSearch::DepthFirstSearch(const Options &opts)
    : SearchEngine(opts),
      single_plan(opts.get<bool>("single_plan")),
      max_depth(0),
      cheapest_plan_cost(INF) {
    if (max_time != numeric_limits<double>::infinity()) {
        cerr << "dfs() doesn't support max_time option." << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}

void DepthFirstSearch::initialize() {
    utils::g_log << "Conducting depth-first search, exclusive bound = "
                 << bound << endl;
}

void DepthFirstSearch::print_statistics() const {
    statistics.print_detailed_statistics();
    utils::g_log << "DFS max depth: " << max_depth << endl;
}

void DepthFirstSearch::recursive_search(const DFSNode &node) {
    int f = node.g;
    if (f >= bound) {
        return;
    }
    if (task_properties::is_goal_state(task_proxy, node.state)) {
        int plan_cost = calculate_plan_cost(operator_sequence, task_proxy);
        utils::g_log << "Found solution with cost " << plan_cost << endl;
        if (plan_cost < cheapest_plan_cost) {
            plan_manager.save_plan(operator_sequence, task_proxy, !single_plan);
            cheapest_plan_cost = plan_cost;
            set_plan(operator_sequence);
            bound = plan_cost;
        }
        return;
    }

    statistics.inc_expanded();
    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(node.state, applicable_ops);
    OperatorsProxy operators = task_proxy.get_operators();
    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = operators[op_id];
        // Temporarily add state to registry and remove it again below.
        int old_registry_size = state_registry.size();
        State succ_state = state_registry.get_successor_state(node.state, op);
        statistics.inc_generated();
        int new_registry_size = state_registry.size();
        assert(new_registry_size >= old_registry_size);
        bool path_to_state_has_cycle = (new_registry_size == old_registry_size);
        if (path_to_state_has_cycle) {
            continue;
        }
        int hashset_index = state_registry.get_hashset_index_of_last_added_state();
        int succ_g = node.g + get_adjusted_cost(op);
        statistics.inc_evaluations();
        statistics.inc_evaluated_states();
        operator_sequence.push_back(op_id);
        int depth = static_cast<int>(operator_sequence.size());
        if (verbosity >= utils::Verbosity::DEBUG && depth > max_depth) {
            utils::g_log << "New DFS max depth: " << max_depth << endl;
        }
        max_depth = max(max_depth, depth);
        DFSNode succ_node(succ_state, succ_g);
        recursive_search(succ_node);
        if (single_plan && found_solution()) {
            return;
        }
        operator_sequence.pop_back();
        state_registry.pop_last_added_state(hashset_index);
        check_invariants();
    }
}

bool DepthFirstSearch::check_invariants() const {
    return operator_sequence.size() + 1 == state_registry.size();
}

SearchStatus DepthFirstSearch::step() {
    utils::g_log << "Starting depth-first search" << endl;
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    DFSNode node(initial_state, 0);
    assert(check_invariants());
    recursive_search(node);
    assert(check_invariants());
    if (found_solution()) {
        return SOLVED;
    }
    return FAILED;
}

void DepthFirstSearch::save_plan_if_necessary() {
    // We don't need to save here, as we automatically save plans when we find them.
}

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Depth-first search",
        "");
    parser.add_option<bool>(
        "single_plan",
        "stop after finding the first plan",
        "false");
    SearchEngine::add_options_to_parser(parser);

    Options opts = parser.parse();

    if (parser.dry_run()) {
        return nullptr;
    }

    return make_shared<depth_first_search::DepthFirstSearch>(opts);
}

static Plugin<SearchEngine> _plugin("dfs", _parse);
}
