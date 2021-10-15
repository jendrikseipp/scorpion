#include "flaw_search.h"

#include "abstraction.h"
#include "transition_system.h"
#include "shortest_paths.h"
#include "../evaluators/g_evaluator.h"
#include "../open_lists/best_first_open_list.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <iterator>
#include <optional.hh>

using namespace std;

namespace cegar {
FlawSearch::FlawSearch(const std::shared_ptr<AbstractTask> &task) :
    task_proxy(*task),
    open_list(nullptr),
    state_registry(nullptr),
    search_space(nullptr),
    statistics(nullptr),
    abstraction(nullptr),
    shortest_paths(nullptr),
    successor_generator(successor_generator::g_successor_generators[task_proxy]),
    g_bound(0) {
    shared_ptr<Evaluator> g_evaluator = make_shared<g_evaluator::GEvaluator>();
    Options options;
    options.set("eval", g_evaluator);
    options.set("pref_only", false);

    open_list = make_shared<standard_scalar_open_list::BestFirstOpenListFactory>(options)->create_state_open_list();
}

void FlawSearch::initialize(const Abstraction *abstraction,
                            const ShortestPaths *shortest_paths) {
    this->abstraction = abstraction;
    this->shortest_paths = shortest_paths;
    open_list->clear();
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    search_space = utils::make_unique_ptr<SearchSpace>(*state_registry);
    statistics = utils::make_unique_ptr<SearchStatistics>(utils::Verbosity::DEBUG);
    State initial_state = state_registry->get_initial_state();
    EvaluationContext eval_context(initial_state, 0, false, statistics.get());
    open_list->insert(eval_context, initial_state.get_id());
}

SearchStatus FlawSearch::step() {
    tl::optional<SearchNode> node;
    // Get non close node. Do we need this loop?
    while (true) {
        if (open_list->empty()) {
            utils::g_log << "Completely explored state space -- no solution!" << endl;
            return FAILED;
        }
        StateID id = open_list->remove_min();
        State s = state_registry->lookup_state(id);
        node.emplace(search_space->get_node(s));

        if (node->is_closed())
            continue;

        EvaluationContext eval_context(s, node->get_g(), false, statistics.get());
        node->close();
        assert(!node->is_dead_end());
        statistics->inc_expanded();
        break;
    }
    g_bound = max(g_bound, node->get_g());

    const State &s = node->get_state();
    if (task_properties::is_goal_state(task_proxy, s)) {
        return SOLVED;
    }

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(s, applicable_ops);
    prune_operators(s, applicable_ops);

    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        State succ_state = state_registry->get_successor_state(s, op);
        statistics->inc_generated();
        SearchNode succ_node = search_space->get_node(succ_state);

        // Previously encountered dead end. Don't re-evaluate.
        if (succ_node.is_dead_end())
            continue;

        if (succ_node.is_new()) {
            // We have not seen this state before.
            // Evaluate and create a new node.

            // Careful: succ_node.get_g() is not available here yet,
            // hence the stupid computation of succ_g.
            // TODO: Make this less fragile.
            int succ_g = node->get_g() + op.get_cost();

            EvaluationContext succ_eval_context(
                succ_state, succ_g, false, statistics.get());
            statistics->inc_evaluated_states();

            if (open_list->is_dead_end(succ_eval_context)) {
                succ_node.mark_as_dead_end();
                statistics->inc_dead_ends();
                continue;
            }
            succ_node.open(*node, op, op.get_cost());

            open_list->insert(succ_eval_context, succ_state.get_id());
        }
    }
    return IN_PROGRESS;
}

void FlawSearch::prune_operators(
    const State &state, std::vector<OperatorID> &ops) const {
    int abstract_state_id = abstraction->get_abstract_state_id(state);
    int h_value = shortest_paths->get_goal_distance(abstract_state_id);

    std::set<OperatorID> tr_ops_set;
    for (const Transition &tr :
         abstraction->get_transition_system().get_outgoing_transitions().at(abstract_state_id)) {
        int target_h_value = shortest_paths->get_goal_distance(tr.target_id);
        int h_value_decrease = h_value - target_h_value;
        if (h_value_decrease == task_proxy.get_operators()[tr.op_id].get_cost()) {
            tr_ops_set.insert(OperatorID(tr.op_id));
        }
    }

    std::set<OperatorID> app_ops_set(ops.begin(), ops.end());

    std::vector<OperatorID> intersection;
    set_intersection(app_ops_set.begin(), app_ops_set.end(),
                     tr_ops_set.begin(), tr_ops_set.end(),
                     std::back_inserter(intersection));
    ops.swap(intersection);
}

void FlawSearch::create_applicability_flaws(
    const State &state,
    const std::vector<OperatorID> &abstract_ops,
    const std::vector<OperatorID> &concrete_ops) const {
    if (!abstract_ops.empty() && concrete_ops.empty()) {
        int abstract_state_id = abstraction->get_abstract_state_id(state);
        const AbstractState *abstact_state =
            &abstraction->get_state(abstract_state_id);

        for (OperatorID id : abstract_ops) {
            /*auto flaw = utils::make_unique_ptr<Flaw>(
                    move(State(state)), *abstract_state,
                    get_cartesian_set(domain_sizes, op.get_preconditions()),
                    FlawReason::NOT_APPLICABLE, choosen_solution);*/
        }
    }
}

// Also part of FlawSelector: refactor
CartesianSet FlawSearch::get_cartesian_set(const vector<int> &domain_sizes,
                                             const ConditionsProxy &conditions) const {
    CartesianSet cartesian_set(domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(condition.get_variable().get_id(),
                                       condition.get_value());
    }
    return cartesian_set;
}

void FlawSearch::search_for_flaws(const Abstraction *abstraction,
                                  const ShortestPaths *shortest_paths) {
    initialize(abstraction, shortest_paths);
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        search_status = step();
    }
    statistics->print_detailed_statistics();
    search_space->print_statistics();
    cout << endl << endl << endl;
}
}
