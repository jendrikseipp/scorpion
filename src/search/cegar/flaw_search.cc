#include "flaw_search.h"

#include "abstraction.h"
#include "abstract_state.h"
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
FlawSearch::FlawSearch(const shared_ptr<AbstractTask> &task, bool debug) :
    task_proxy(*task),
    open_list(nullptr),
    state_registry(nullptr),
    search_space(nullptr),
    statistics(nullptr),
    abstraction(nullptr),
    shortest_paths(nullptr),
    domain_sizes(nullptr),
    successor_generator(
        successor_generator::g_successor_generators[task_proxy]),
    g_bound(0),
    f_bound(0),
    debug(debug) {
    shared_ptr<Evaluator> g_evaluator = make_shared<g_evaluator::GEvaluator>();
    Options options;
    options.set("eval", g_evaluator);
    options.set("pref_only", false);

    open_list = make_shared<standard_scalar_open_list::BestFirstOpenListFactory>(options)->create_state_open_list();
}

void FlawSearch::initialize(const std::vector<int> *domain_sizes,
                            const Abstraction *abstraction,
                            const ShortestPaths *shortest_paths) {
    g_bound = 0;
    int new_f_bound = shortest_paths->get_goal_distance(abstraction->get_initial_state().get_id());
    assert(new_f_bound >= f_bound);
    if (new_f_bound > f_bound) {
        utils::g_log << "New abstract bound: " << new_f_bound << endl;
    }
    f_bound = new_f_bound;
    applicability_flaws = map<int, vector<Flaw>>();
    deviation_flaws = map<int, vector<Flaw>>();
    this->domain_sizes = domain_sizes;
    this->abstraction = abstraction;
    this->shortest_paths = shortest_paths;
    open_list->clear();
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    search_space = utils::make_unique_ptr<SearchSpace>(*state_registry);
    statistics = utils::make_unique_ptr<SearchStatistics>(utils::Verbosity::DEBUG);

    State initial_state = state_registry->get_initial_state();
    EvaluationContext eval_context(initial_state, 0, false, statistics.get());
    // open_list->insert(eval_context, initial_state.get_id());

    if (open_list->is_dead_end(eval_context)) {
        utils::g_log << "Initial state is a dead end." << endl;
    } else {
        SearchNode node = search_space->get_node(initial_state);
        node.open_initial();

        open_list->insert(eval_context, initial_state.get_id());
    }
}

SearchStatus FlawSearch::step() {
    tl::optional<SearchNode> node;
    // Get non close node. Do we need this loop?
    while (true) {
        if (open_list->empty()) {
            /// utils::g_log << "Completely explored state space -- no solution!" << endl;
            return FAILED;
        }
        StateID id = open_list->remove_min();
        State s = state_registry->lookup_state(id);
        node.emplace(search_space->get_node(s));

        if (node->is_closed()) {
            continue;
        }

        int abstract_goal_distance =
            shortest_paths->get_goal_distance(abstraction->get_abstract_state_id(s));

        if (node->get_real_g() + abstract_goal_distance > f_bound) {
            continue;
        }

        EvaluationContext eval_context(s, node->get_g(), false, statistics.get());
        node->close();
        assert(!node->is_dead_end());
        statistics->inc_expanded();
        break;
    }
    g_bound = max(g_bound, node->get_real_g());

    const State &s = node->get_state();
    if (task_properties::is_goal_state(task_proxy, s)) {
        Plan plan;
        search_space->trace_path(s, plan);
        PlanManager plan_mgr;
        cout << "FOUND CONCRETE SOLUTION: " << endl;
        plan_mgr.save_plan(plan, task_proxy, true);
        return SOLVED;
    }

    // utils::g_log << "ABS STATE: " << abstraction->get_abstract_state_id(s)
    //              << " (g: " << node->get_real_g()
    //              << ", h: " << shortest_paths->get_goal_distance(
    //     abstraction->get_abstract_state_id(s)) << ")" << endl;

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(s, applicable_ops);
    vector<OperatorID> abstraction_ops;
    generate_abstraction_operators(s, abstraction_ops);
    vector<OperatorID> valid_ops;
    prune_operators(applicable_ops, abstraction_ops, valid_ops);

    // utils::g_log << "APP: " << applicable_ops << endl;
    // utils::g_log << "ABST: " << abstraction_ops << endl;
    // utils::g_log << "VAL: " << valid_ops << endl;
    // utils::g_log << endl;

    create_applicability_flaws(s, abstraction_ops, valid_ops);

    for (OperatorID op_id : valid_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        State succ_state = state_registry->get_successor_state(s, op);
        statistics->inc_generated();
        SearchNode succ_node = search_space->get_node(succ_state);

        bool valid_succ_state = create_deviation_flaws(s, succ_state, op_id);
        if (!valid_succ_state) {
            continue;
        }

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

void FlawSearch::generate_abstraction_operators(
    const State &state,
    std::vector<OperatorID> &abstraction_ops) const {
    int abstract_state_id = abstraction->get_abstract_state_id(state);
    int h_value = shortest_paths->get_goal_distance(abstract_state_id);

    set<OperatorID> abstraction_ops_set;
    for (const Transition &tr :
         abstraction->get_transition_system().get_outgoing_transitions().at(abstract_state_id)) {
        int target_h_value = shortest_paths->get_goal_distance(tr.target_id);
        int h_value_decrease = h_value - target_h_value;
        int op_cost = task_proxy.get_operators()[tr.op_id].get_cost();
        if (h_value_decrease == op_cost && (h_value - op_cost == target_h_value)) {
            abstraction_ops_set.insert(OperatorID(tr.op_id));
        }
    }

    copy(abstraction_ops_set.begin(), abstraction_ops_set.end(),
         std::back_inserter(abstraction_ops));
}

void FlawSearch::prune_operators(
    const std::vector<OperatorID> &applicable_ops,
    const std::vector<OperatorID> &abstraction_ops,
    std::vector<OperatorID> &valid_ops) const {
    set<OperatorID> applicable_ops_set(
        applicable_ops.begin(), applicable_ops.end());
    set<OperatorID> abstraction_ops_set(
        abstraction_ops.begin(), abstraction_ops.end());
    set_intersection(applicable_ops_set.begin(), applicable_ops_set.end(),
                     abstraction_ops_set.begin(), abstraction_ops_set.end(),
                     back_inserter(valid_ops));
}


void FlawSearch::create_applicability_flaws(
    const State &state,
    const vector<OperatorID> &abstraction_ops,
    const vector<OperatorID> &valid_ops) const {
    if (!abstraction_ops.empty() && valid_ops.empty()) {
        int abstract_state_id = abstraction->get_abstract_state_id(state);
        const AbstractState *abstract_state =
            &abstraction->get_state(abstract_state_id);
        int h_value = shortest_paths->get_goal_distance(abstract_state_id);

        for (OperatorID op_id : abstraction_ops) {
            if (applicability_flaws.find(g_bound) == applicability_flaws.end()) {
                applicability_flaws[g_bound] = vector<Flaw>();
            }

            vector<Transition> valid_transitions;
            for (const Transition &tr :
                 abstraction->get_transition_system().get_outgoing_transitions().at(abstract_state_id)) {
                if (tr.op_id != op_id.get_index()) {
                    continue;
                }
                int target_h_value = shortest_paths->get_goal_distance(tr.target_id);
                int h_value_decrease = h_value - target_h_value;
                int op_cost = task_proxy.get_operators()[tr.op_id].get_cost();
                if (h_value_decrease == op_cost && (h_value - op_cost == target_h_value)) {
                    valid_transitions.push_back(tr);
                }
            }

            for (const Transition &tr : valid_transitions) {
                applicability_flaws[g_bound].emplace_back(
                    move(State(state)),
                    *abstract_state,
                    get_cartesian_set(task_proxy.get_operators()[op_id].get_preconditions()),
                    FlawReason::NOT_APPLICABLE,
                    get_abstract_solution(state, abstraction->get_state(tr.target_id), tr));
            }
        }
    }
}

bool FlawSearch::create_deviation_flaws(
    const State &state,
    const State &next_state,
    const OperatorID op_id) const {
    bool valid_transition = false;
    int abstract_state_id = abstraction->get_abstract_state_id(state);
    int next_abstract_state_id = abstraction->get_abstract_state_id(next_state);
    const AbstractState *abstract_state =
        &abstraction->get_state(abstract_state_id);
    const AbstractState *next_abstract_state =
        &abstraction->get_state(next_abstract_state_id);
    int h_value = shortest_paths->get_goal_distance(abstract_state_id);

    vector<Transition> valid_transitions;
    for (const Transition &tr :
         abstraction->get_transition_system().get_outgoing_transitions().at(abstract_state_id)) {
        if (tr.op_id != op_id.get_index()) {
            continue;
        }
        int target_h_value = shortest_paths->get_goal_distance(tr.target_id);
        int h_value_decrease = h_value - target_h_value;
        int op_cost = task_proxy.get_operators()[tr.op_id].get_cost();
        if (h_value_decrease == op_cost && (h_value - op_cost == target_h_value)) {
            valid_transitions.push_back(tr);
        }
    }

    for (const Transition &tr : valid_transitions) {
        if (tr.target_id != next_abstract_state_id) {
            if (deviation_flaws.find(g_bound) == deviation_flaws.end()) {
                deviation_flaws[g_bound] = vector<Flaw>();
            }
            const AbstractState *deviated_abstact_state =
                &abstraction->get_state(tr.target_id);

            deviation_flaws[g_bound].emplace_back(
                move(State(state)),
                *abstract_state,
                deviated_abstact_state->regress(task_proxy.get_operators()[op_id]),
                FlawReason::PATH_DEVIATION,
                get_abstract_solution(state, *deviated_abstact_state, tr));
        } else {
            valid_transition = true;
        }
    }
    return valid_transition;
}

Solution FlawSearch::get_abstract_solution(
    const State &concrete_state,
    const AbstractState &flawed_abstract_state,
    const Transition &flawed_tr) const {
    Solution sol;
    vector<State> path;
    Plan plan;
    search_space->generate_solution_trace(concrete_state, path, plan);

    int cur_abstract_state_id = abstraction->get_initial_state().get_id();
    // construct path to flawed_abstract_state based on partial plan
    for (size_t i = 0; i < plan.size(); ++i) {
        const OperatorID &op_id = plan.at(i);
        int next_abstract_state_id = abstraction->get_abstract_state_id(path.at(i + 1));
        for (const Transition &tr :
             abstraction->get_transition_system().get_outgoing_transitions().at(cur_abstract_state_id)) {
            if (tr.op_id == op_id.get_index() && tr.target_id == next_abstract_state_id) {
                sol.push_back(tr);
                cur_abstract_state_id = tr.target_id;
            }
        }
    }
    assert(sol.size() == plan.size());
    if (flawed_tr.is_defined()) {
        sol.push_back(flawed_tr);
    }

    // shortest path to goal state (siffix_solution)
    Solution suffix_sol = shortest_paths->get_shortest_path(
            flawed_abstract_state.get_id());
    sol.insert(sol.end(), suffix_sol.begin(), suffix_sol.end());
    return sol;
}

// Also part of FlawSelector: refactor
CartesianSet FlawSearch::get_cartesian_set(const ConditionsProxy &conditions) const {
    CartesianSet cartesian_set(*domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(condition.get_variable().get_id(),
                                       condition.get_value());
    }
    return cartesian_set;
}

unique_ptr<Flaw>
FlawSearch::search_for_flaws(const std::vector<int> *domain_sizes,
                             const Abstraction *abstraction,
                             const ShortestPaths *shortest_paths) {
    initialize(domain_sizes, abstraction, shortest_paths);
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        search_status = step();
    }
    // search_space->dump(task_proxy);

    if (debug) {
        utils::g_log << "Apply-flaws:" << endl;
        for (auto pair : applicability_flaws) {
            utils::g_log << "\t" << pair.first << ": " << pair.second.size() << endl;
        }
        utils::g_log << "dev-flaws" << endl;
        for (auto pair : deviation_flaws) {
            utils::g_log << "\t" << pair.first << ": " << pair.second.size() << endl;
        }
    }


    if (search_status == FAILED) {
        assert(!applicability_flaws.empty() || !deviation_flaws.empty());

        int max_g_applicability_flaw = -1;
        if (!applicability_flaws.empty()) {
            max_g_applicability_flaw = applicability_flaws.rbegin()->first;
        }
        int max_g_deviation_flaw = -1;
        if (!deviation_flaws.empty()) {
            max_g_deviation_flaw = deviation_flaws.rbegin()->first;
        }
        Flaw result =
            max_g_deviation_flaw >= max_g_applicability_flaw ?
            deviation_flaws.rbegin()->second.at(0) :
            applicability_flaws.rbegin()->second.at(0);

        // for (auto pair : deviation_flaws) {
        //     for (const Flaw flaw : pair.second) {
        //         cout << "DEV SOL:" << endl;
        //         for (const Transition &t : flaw.flawed_solution) {
        //             OperatorProxy op = task_proxy.get_operators()[t.op_id];
        //             cout << "  " << t << " (" << op.get_name() << ", " << op.get_cost() << ") ID: "
        //                  << op.get_id() << endl;
        //         }
        //     }
        // }
        return utils::make_unique_ptr<Flaw>(result);
    }
    return nullptr;

    //statistics->print_detailed_statistics();
    // search_space->print_statistics();
}
}
