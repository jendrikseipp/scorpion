#include "exhaustive_search.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <cassert>

using namespace std;

namespace exhaustive_search {
ExhaustiveSearch::ExhaustiveSearch(const Options &opts)
    : SearchEngine(opts) {
    assert(cost_type == ONE);
}

void ExhaustiveSearch::initialize() {
    utils::g_log << "Dumping the reachable state space..." << endl;
    cout << "# G: goal state" << endl;
    cout << "# T: transition" << endl;
    cout << "# The initial state has ID 0." << endl;
    assert(state_registry.size() <= 1);
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    // The initial state has id 0, so we'll start there.
    current_state_id = 0;
}

void ExhaustiveSearch::print_statistics() const {
    statistics.print_detailed_statistics();
    search_space.print_statistics();
}

SearchStatus ExhaustiveSearch::step() {
    if (current_state_id == static_cast<int>(state_registry.size())) {
        utils::g_log << "Finished dumping the reachable state space." << endl;
        return FAILED;
    }

    State s = state_registry.lookup_state(StateID(current_state_id));
    statistics.inc_expanded();
    if (task_properties::is_goal_state(task_proxy, s)) {
        cout << "G " << s.get_id().value << endl;
    }

    /* Next time we'll look at the next state that was created in the registry.
       This results in a breadth-first order. */
    ++current_state_id;

    vector<OperatorID> applicable_op_ids;
    successor_generator.generate_applicable_ops(s, applicable_op_ids);

    OperatorsProxy operators = task_proxy.get_operators();
    for (OperatorID op_id : applicable_op_ids) {
        // Add successor states to registry.
        State succ_state = state_registry.get_successor_state(s, operators[op_id]);
        statistics.inc_generated();
        cout << "T " << s.get_id().value << " " << succ_state.get_id().value << endl;
    }
    return IN_PROGRESS;
}

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Exhaustive search",
        "Dump the reachable state space.");
    utils::add_log_options_to_parser(parser);

    Options opts = parser.parse();

    opts.set<OperatorCost>("cost_type", ONE);
    opts.set<int>("bound", numeric_limits<int>::max());
    opts.set<double>("max_time", numeric_limits<double>::infinity());

    if (parser.dry_run()) {
        return nullptr;
    }

    return make_shared<ExhaustiveSearch>(opts);
}

static Plugin<SearchEngine> _plugin("dump_reachable_search_space", _parse);
}
