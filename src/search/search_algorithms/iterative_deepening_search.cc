#include "iterative_deepening_search.h"

#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"

#include "../utils/logging.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace iterative_deepening_search {
IterativeDeepeningSearch::IterativeDeepeningSearch(
    bool single_plan, OperatorCost cost_type, int bound, double max_time,
    const string &description, utils::Verbosity verbosity)
    : SearchAlgorithm(cost_type, bound, max_time, description, verbosity),
      single_plan(single_plan),
      sg(task_proxy),
      last_plan_cost(-1) {
    if (!task_properties::is_unit_cost(task_proxy)) {
        cerr << "Iterative deepening search only supports unit-cost tasks." << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}

void IterativeDeepeningSearch::initialize() {
    utils::g_log << "Conducting iterative deepening search, (real) bound = " << bound << endl;
}

void IterativeDeepeningSearch::print_statistics() const {
    statistics.print_detailed_statistics();
}

void IterativeDeepeningSearch::recursive_search(const State &state, int depth_limit) {
    if (task_properties::is_goal_state(task_proxy, state)) {
        int plan_cost = calculate_plan_cost(operator_sequence, task_proxy);
        if (plan_cost > last_plan_cost) {
            plan_manager.save_plan(operator_sequence, task_proxy, !single_plan);
            last_plan_cost = plan_cost;
            set_plan(operator_sequence);
        }
        return;
    }

    if (depth_limit > 0) {
        statistics.inc_expanded();
        OperatorsProxy operators = task_proxy.get_operators();
        const vector<int> &applicable_operators = sg.get_applicable_operators();
#ifndef NDEBUG
        vector<OperatorID> applicable_ops;
        successor_generator.generate_applicable_ops(state, applicable_ops);
        unordered_set<int> old_ops;
        for (OperatorID op_id : applicable_ops) {
            old_ops.insert(op_id.get_index());
        }
        assert(unordered_set<int>(applicable_operators.begin(), applicable_operators.end()) == old_ops);
#endif
        for (int op_id : applicable_operators) {
            OperatorProxy op = operators[op_id];
            State succ_state = state.get_unregistered_successor(op);
            statistics.inc_generated();
            sg.push_transition(state, op.get_id());
            operator_sequence.emplace_back(op_id);
            recursive_search(succ_state, depth_limit - 1);
            operator_sequence.pop_back();
            sg.pop_transition(state, op.get_id());
            if (found_solution() && single_plan) {
                return;
            }
        }
    }
}

SearchStatus IterativeDeepeningSearch::step() {
    State initial_state = task_proxy.get_initial_state();
    sg.reset_to_state(initial_state);
    for (int depth_limit = 0;
         (!single_plan || !found_solution()) && depth_limit < bound;
         ++depth_limit) {
        utils::g_log << "depth limit: " << depth_limit << endl;
        recursive_search(initial_state, depth_limit);
    }
    if (found_solution()) {
        return SOLVED;
    }
    return FAILED;
}

void IterativeDeepeningSearch::save_plan_if_necessary() {
    // We don't need to save here, as we automatically save plans when we find them.
}

class IterativeDeepeningSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, iterative_deepening_search::IterativeDeepeningSearch> {
public:
    IterativeDeepeningSearchFeature() : TypedFeature("ids") {
        document_title("Iterative deepening search");
        add_option<bool>(
            "single_plan",
            "stop after finding the first (shortest) plan",
            "true");
        add_search_algorithm_options_to_feature(*this, "ids");
    }

    virtual shared_ptr<IterativeDeepeningSearch> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<IterativeDeepeningSearch>(
            options.get<bool>("single_plan"),
            get_search_algorithm_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<IterativeDeepeningSearchFeature> _plugin;
}
