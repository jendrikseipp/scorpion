#include "breadth_first_search.h"

#include "search_common.h"

#include "../pruning_method.h"

#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace breadth_first_search {
BreadthFirstSearch::BreadthFirstSearch(
    bool single_plan, bool write_plan, const shared_ptr<PruningMethod> &pruning,
    const string &description, utils::Verbosity verbosity)
    : SearchAlgorithm(
          ONE, numeric_limits<int>::max(),
          numeric_limits<double>::infinity(), description, verbosity),
      single_plan(single_plan),
      write_plan(write_plan),
      last_plan_cost(-1),
      pruning_method(pruning) {
}

void BreadthFirstSearch::initialize() {
    utils::g_log << "Conducting breadth-first search" << endl;
    assert(state_registry.size() <= 1);
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    // The initial state has id 0, so we'll start there.
    current_state_id = 0;
    if (write_plan) {
        // The parent pointer of the initial state is undefined.
        parents[initial_state] = Parent();
    }
    pruning_method->initialize(task);
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
        if (found_solution()) {
            utils::g_log << "Completely explored state space -- found solution." << endl;
            return SOLVED;
        } else {
            utils::g_log << "Completely explored state space -- no solution!" << endl;
            return UNSOLVABLE;
        }
    }

    State s = state_registry.lookup_state(StateID(current_state_id));
    statistics.inc_expanded();
    /* Next time we'll look at the next state that was created in the registry.
       This results in a breadth-first order. */
    ++current_state_id;

    if (task_properties::is_goal_state(task_proxy, s)) {
        vector<OperatorID> plan = trace_path(s);
        int plan_cost = calculate_plan_cost(plan, task_proxy);
        if (plan_cost > last_plan_cost) {
            plan_manager.save_plan(plan, task_proxy, !single_plan);
            last_plan_cost = plan_cost;
            set_plan(plan);
        }
        if (single_plan) {
            return SOLVED;
        }
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

void BreadthFirstSearch::save_plan_if_necessary() {
    // We don't need to save here, as we automatically save plans when we find them.
}

class BreadthFirstSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, BreadthFirstSearch> {
public:
    BreadthFirstSearchFeature() : TypedFeature("brfs") {
        document_title("Breadth-first search");
        document_synopsis("Breadth-first graph search.");
        add_option<bool>(
            "single_plan",
            "Stop search after finding the first (shortest) plan.",
            "true");
        add_option<bool>(
            "write_plan",
            "Store the necessary information during search for writing plans once "
            "they're found.",
            "true");
        add_option<shared_ptr<PruningMethod>>(
            "pruning",
            "Pruning methods can prune or reorder the set of applicable operators in "
            "each state and thereby influence the number and order of successor states "
            "that are considered.",
            "null()");
        add_option<string>(
            "description",
            "description used to identify search algorithm in logs",
            "\"brfs\"");
        utils::add_log_options_to_feature(*this);
    }

    virtual shared_ptr<BreadthFirstSearch> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<BreadthFirstSearch>(
            options.get<bool>("single_plan"),
            options.get<bool>("write_plan"),
            options.get<shared_ptr<PruningMethod>>("pruning"),
            options.get<string>("description"),
            utils::get_log_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<BreadthFirstSearchFeature> _plugin;
}
