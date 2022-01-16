#include "breadth_first_search.h"

#include "search_common.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../pruning_method.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace breadth_first_search {
BreadthFirstSearch::BreadthFirstSearch(const Options &opts)
    : SearchEngine(opts),
      write_plan(opts.get<bool>("write_plan")),
      pruning_method(opts.get<shared_ptr<PruningMethod>>("pruning")) {
    assert(cost_type == ONE);
}

void BreadthFirstSearch::initialize() {
    utils::g_log << "Conducting breadth-first search" << endl;
    assert(state_registry.size() <= 1);
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    // The initial state has id 0, so we'll start there.
    current_state_id = 0;
    if (write_plan) {
        // THe parent pointer of the initial state is undefined.
        parents[initial_state] = Parent();
    }
}

void BreadthFirstSearch::print_statistics() const {
    statistics.print_detailed_statistics();
    search_space.print_statistics();
    pruning_method->print_statistics();
}

vector<OperatorID> BreadthFirstSearch::trace_path(const State &goal_state) const {
    assert(goal_state.get_registry() == &state_registry);
    StateID current_state_id = goal_state.get_id();
    vector<OperatorID> path;
    for (;;) {
        const Parent &parent = parents[state_registry.lookup_state(current_state_id)];
        if (parent.op_id == OperatorID::no_operator) {
            assert(parent.state_id == StateID::no_state);
            break;
        }
        path.push_back(parent.op_id);
        assert(current_state_id != parent.state_id);
        current_state_id = parent.state_id;
    }
    reverse(path.begin(), path.end());
    return path;
}

SearchStatus BreadthFirstSearch::step() {
    if (current_state_id == static_cast<int>(state_registry.size())) {
        // We checked all states in the registry without finding a goal state.
        utils::g_log << "Completely explored state space -- no solution!" << endl;
        return UNSOLVABLE;
    }

    State s = state_registry.lookup_state(StateID(current_state_id));
    statistics.inc_expanded();
    /* Next time we'll look at the next state that was created in the registry.
       This results in a breadth-first order. */
    ++current_state_id;

    if (task_properties::is_goal_state(task_proxy, s)) {
        utils::g_log << "Solution found!" << endl;
        vector<OperatorID> plan = trace_path(s);
        set_plan(plan);
        return SOLVED;
    }

    vector<OperatorID> applicable_op_ids;
    successor_generator.generate_applicable_ops(s, applicable_op_ids);

    pruning_method->prune_operators(s, applicable_op_ids);

    OperatorsProxy operators = task_proxy.get_operators();
    for (OperatorID op_id : applicable_op_ids) {
        int old_num_states = state_registry.size();
        State succ_state = state_registry.get_successor_state(s, operators[op_id]);
        statistics.inc_generated();
        int new_num_states = state_registry.size();
        bool is_new_state = (new_num_states > old_num_states);
        if (is_new_state && write_plan) {
            parents[succ_state] = Parent(s.get_id(), op_id);
        }
    }
    return IN_PROGRESS;
}

static void add_pruning_option(OptionParser &parser) {
    parser.add_option<shared_ptr<PruningMethod>>(
        "pruning",
        "Pruning methods can prune or reorder the set of applicable operators in "
        "each state and thereby influence the number and order of successor states "
        "that are considered.",
        "null()");
}

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Breadth-first search",
        "Breadth-first graph search.");

    parser.add_option<bool>(
        "write_plan",
        "Store the necessary information during search for writing the plan once "
        "it's found. Set to false if the plan is irrelevant.",
        "true");

    add_pruning_option(parser);

    Options opts = parser.parse();

    opts.set<OperatorCost>("cost_type", ONE);
    opts.set<int>("bound", numeric_limits<int>::max());
    opts.set<double>("max_time", numeric_limits<double>::infinity());
    opts.set<utils::Verbosity>("verbosity", utils::Verbosity::NORMAL);

    if (parser.dry_run()) {
        return nullptr;
    }

    return make_shared<BreadthFirstSearch>(opts);
}

static Plugin<SearchEngine> _plugin("brfs", _parse);
}
