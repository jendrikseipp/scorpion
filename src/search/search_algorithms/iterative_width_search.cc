#include "iterative_width_search.h"

#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../utils/logging.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace iterative_width_search {
IterativeWidthSearch::IterativeWidthSearch(
    int width, OperatorCost cost_type, int bound, double max_time,
    const string &description, utils::Verbosity verbosity)
    : SearchAlgorithm(cost_type, bound, max_time, description, verbosity),
      novelty_table(task_proxy, width) {
    utils::g_log << "Setting up iterative width search." << endl;
}

void IterativeWidthSearch::initialize() {
    utils::g_log << "Starting iterative width search." << endl;
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    SearchNode node = search_space.get_node(initial_state);
    node.open_initial();
    open_list.push_back(initial_state.get_id());
    bool novel = is_novel(initial_state);
    utils::unused_variable(novel);
    assert(novel);
}

bool IterativeWidthSearch::is_novel(const State &state) {
    return novelty_table.compute_novelty_and_update_table(state) < 3;
}

bool IterativeWidthSearch::is_novel(const OperatorProxy &op, const State &succ_state) {
    return novelty_table.compute_novelty_and_update_table(op, succ_state) < 3;
}

void IterativeWidthSearch::print_statistics() const {
    novelty_table.print_statistics();
    statistics.print_detailed_statistics();
    search_space.print_statistics();
}

SearchStatus IterativeWidthSearch::step() {
    if (open_list.empty()) {
        utils::g_log << "Completely explored state space -- no solution!" << endl;
        return FAILED;
    }
    StateID id = open_list.front();
    open_list.pop_front();
    State state = state_registry.lookup_state(id);
    SearchNode node = search_space.get_node(state);
    node.close();
    assert(!node.is_dead_end());
    statistics.inc_expanded();

    if (check_goal_and_set_plan(state)) {
        return SOLVED;
    }

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(state, applicable_ops);
    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        if (node.get_real_g() + op.get_cost() >= bound) {
            continue;
        }

        State succ_state = state_registry.get_successor_state(state, op);
        statistics.inc_generated();

        bool novel = is_novel(op, succ_state);

        if (!novel) {
            continue;
        }

        SearchNode succ_node = search_space.get_node(succ_state);
        assert(succ_node.is_new());
        succ_node.open_new_node(node, op, get_adjusted_cost(op));
        open_list.push_back(succ_state.get_id());
    }

    return IN_PROGRESS;
}

void IterativeWidthSearch::dump_search_space() const {
    search_space.dump(task_proxy);
}

class IterativeWidthSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, iterative_width_search::IterativeWidthSearch> {
public:
    IterativeWidthSearchFeature() : TypedFeature("iw") {
        document_title("Iterated width search");
        add_option<int>(
            "width", "maximum conjunction size", "2", plugins::Bounds("1", "2"));
        add_search_algorithm_options_to_feature(*this, "iw");
    }

    virtual shared_ptr<IterativeWidthSearch> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<IterativeWidthSearch>(
            options.get<int>("width"),
            get_search_algorithm_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<IterativeWidthSearchFeature> _plugin;
}
