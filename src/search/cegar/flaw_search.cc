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
bool compare_flaws(std::shared_ptr<Flaw> &a, std::shared_ptr<Flaw> &b) {
    assert(a && b);
    return a->h_value > b->h_value
           || (a->h_value == b->h_value && static_cast<int>(a->flaw_reason) < static_cast<int>(b->flaw_reason));
}

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

void FlawSearch::initialize(const vector<int> *domain_sizes,
                            const Abstraction *abstraction,
                            const ShortestPaths *shortest_paths) {
    g_bound = 0;
    int new_f_bound = shortest_paths->get_goal_distance(abstraction->get_initial_state().get_id());
    assert(new_f_bound >= f_bound);
    if (new_f_bound > f_bound) {
        utils::g_log << "New abstract bound: " << new_f_bound << endl;
    }
    f_bound = new_f_bound;
    this->domain_sizes = domain_sizes;
    this->abstraction = abstraction;
    this->shortest_paths = shortest_paths;
    open_list->clear();
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    search_space = utils::make_unique_ptr<SearchSpace>(*state_registry);
    statistics = utils::make_unique_ptr<SearchStatistics>(utils::Verbosity::DEBUG);

    flaws = priority_queue<shared_ptr<Flaw>,
                           vector<shared_ptr<Flaw>>,
                           decltype( &compare_flaws)>(compare_flaws);

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

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(s, applicable_ops);

    vector<OperatorID> abstraction_ops;
    vector<Transition> abstraction_trs;
    generate_abstraction_operators(s, abstraction_ops, abstraction_trs);

    vector<OperatorID> valid_ops;
    vector<Transition> valid_trs;
    vector<Transition> invalid_trs;
    prune_operators(applicable_ops, abstraction_ops, abstraction_trs,
                    valid_ops, valid_trs, invalid_trs);

    if (debug) {
        utils::g_log << "STATE " <<
            abstraction->get_abstract_state_id(s) << ":" << endl;
        utils::g_log << "All trs: " << abstraction_trs << endl;
        utils::g_log << "Applicable trs: " << valid_trs << endl;
        utils::g_log << "Inapplicable trs: " << invalid_trs << endl;
    }

    if (!invalid_trs.empty()) {
        create_applicability_flaws(s, invalid_trs);
    }

    for (const OperatorID &op_id : valid_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        State succ_state = state_registry->get_successor_state(s, op);
        statistics->inc_generated();
        SearchNode succ_node = search_space->get_node(succ_state);

        bool valid_succ_state = create_deviation_flaws(s, succ_state, op_id, valid_trs);
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
    vector<OperatorID> &abstraction_ops,
    vector<Transition> &abstraction_trs) const {
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
            if (find(abstraction_trs.begin(), abstraction_trs.end(), tr)
                == abstraction_trs.end()) {
                abstraction_trs.push_back(tr);
            }
        }
    }

    copy(abstraction_ops_set.begin(), abstraction_ops_set.end(),
         back_inserter(abstraction_ops));
}

void FlawSearch::prune_operators(
    const vector<OperatorID> &applicable_ops,
    const vector<OperatorID> &abstraction_ops,
    const vector<Transition> &abstraction_trs,
    vector<OperatorID> &valid_ops,
    vector<Transition> &valid_trs,
    vector<Transition> &invalid_trs) const {
    set<OperatorID> applicable_ops_set(
        applicable_ops.begin(), applicable_ops.end());
    set<OperatorID> abstraction_ops_set(
        abstraction_ops.begin(), abstraction_ops.end());
    set_intersection(applicable_ops_set.begin(), applicable_ops_set.end(),
                     abstraction_ops_set.begin(), abstraction_ops_set.end(),
                     back_inserter(valid_ops));

    for (const Transition &tr : abstraction_trs) {
        if (find(valid_ops.begin(), valid_ops.end(), OperatorID(tr.op_id))
            != valid_ops.end()) {
            valid_trs.push_back(tr);
        } else {
            invalid_trs.push_back(tr);
        }
    }
}

void FlawSearch::create_applicability_flaws(
    const State &state,
    const vector<Transition> &invalid_trs) const {
    int abstract_state_id = abstraction->get_abstract_state_id(state);
    const AbstractState *abstract_state =
        &abstraction->get_state(abstract_state_id);

    int h_value = shortest_paths->get_goal_distance(abstract_state_id);

    for (const Transition &tr : invalid_trs) {
        Flaw flaw(move(State(state)),
                  *abstract_state,
                  get_cartesian_set(task_proxy.get_operators()[tr.op_id].get_preconditions()),
                  FlawReason::NOT_APPLICABLE,
                  Solution(), h_value);
        flaws.push(make_shared<Flaw>(flaw));
        if (debug) {
            Solution sol = get_abstract_solution(
                    state, abstraction->get_state(tr.target_id), tr);
            utils::g_log << "Inapplicable flaw: #" << abstract_state_id <<
                flaw.desired_cartesian_set << " with plan " << endl;
            for (const Transition &t : sol) {
                OperatorProxy op = task_proxy.get_operators()[t.op_id];
                utils::g_log << "  " << t << " (" << op.get_name() << ", " << op.get_cost() << ")" << endl;
            }
        }
    }
}

bool FlawSearch::create_deviation_flaws(
    const State &state,
    const State &next_state,
    const OperatorID &op_id,
    const vector<Transition> &valid_trs) const {
    bool valid_transition = false;
    int abstract_state_id = abstraction->get_abstract_state_id(state);
    int next_abstract_state_id = abstraction->get_abstract_state_id(next_state);
    const AbstractState *abstract_state =
        &abstraction->get_state(abstract_state_id);

    int h_value = shortest_paths->get_goal_distance(abstract_state_id);

    for (const Transition &tr : valid_trs) {
        if (tr.op_id != op_id.get_index()) {
            continue;
        }

        if (tr.target_id != next_abstract_state_id) {
            const AbstractState *deviated_abstact_state =
                &abstraction->get_state(tr.target_id);

            Flaw flaw(move(State(state)),
                      *abstract_state,
                      deviated_abstact_state->regress(task_proxy.get_operators()[op_id]),
                      FlawReason::PATH_DEVIATION,
                      Solution(), h_value);

            flaws.push(make_shared<Flaw>(flaw));
            //flaws.push(deviation_flaws[h_value].back());
            if (debug) {
                Solution sol = get_abstract_solution(
                        state, *deviated_abstact_state, tr);
                utils::g_log << "Deviation flaw: #" << abstract_state_id << flaw.desired_cartesian_set << " with plan " << endl;
                for (const Transition &t : sol) {
                    OperatorProxy op = task_proxy.get_operators()[t.op_id];
                    utils::g_log << "  " << t << " (" << op.get_name() << ", " << op.get_cost() << ")" << endl;
                }
            }
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
FlawSearch::search_for_flaws(const vector<int> *domain_sizes,
                             const Abstraction *abstraction,
                             const ShortestPaths *shortest_paths) {
    initialize(domain_sizes, abstraction, shortest_paths);
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        search_status = step();
    }

    if (debug) {
        utils::g_log << "Found " << flaws.size() << " flaws!" << endl;
    }

    if (search_status == FAILED) {
        // This is an ugly conversion we want to avoid!
        return utils::make_unique_ptr<Flaw>(*flaws.top());
    }
    return nullptr;

    // search_space->dump(task_proxy);
    //statistics->print_detailed_statistics();
    // search_space->print_statistics();
}

unique_ptr<Flaw>
FlawSearch::get_next_flaw(const vector<int> *domain_sizes,
                          const Abstraction *abstraction,
                          const ShortestPaths *shortest_paths) {
    while (!flaws.empty()) {
        auto flaw = flaws.top();
        flaws.pop();
        int new_abstract_state_id =
            abstraction->get_abstract_state_id(flaw->concrete_state);
        int new_h_value =
            shortest_paths->get_goal_distance(new_abstract_state_id);

        // Path was not affected (TODO: something goes wrong here)
        if (flaw->current_abstract_state.get_id() == new_abstract_state_id &&
            flaw->h_value == new_h_value) {
            // This is an ugly conversion we want to avoid!
            return utils::make_unique_ptr<Flaw>(*flaw);
        }
    }

    // Search for new flaws
    auto flaw = search_for_flaws(domain_sizes, abstraction, shortest_paths);
    return flaw;
}
}
