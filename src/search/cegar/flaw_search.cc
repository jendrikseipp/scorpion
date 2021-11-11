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
FlawSearch::FlawSearch(const shared_ptr<AbstractTask> &task,
                       const vector<int> &domain_sizes,
                       const Abstraction &abstraction,
                       const ShortestPaths &shortest_paths,
                       bool debug) :
    task_proxy(*task),
    open_list(nullptr),
    state_registry(utils::make_unique_ptr<StateRegistry>(task_proxy)),
    search_space(nullptr),
    statistics(nullptr),
    domain_sizes(domain_sizes),
    abstraction(abstraction),
    shortest_paths(shortest_paths),
    successor_generator(
        successor_generator::g_successor_generators[task_proxy]),
    g_bound(0),
    f_bound(0),
    debug(debug),
    min_flaw_h(INF),
    num_searches(0),
    num_overall_refined_flaws(0),
    num_overall_expanded_concrete_states(0) {
    shared_ptr<Evaluator> g_evaluator = make_shared<g_evaluator::GEvaluator>();
    Options options;
    options.set("eval", g_evaluator);
    options.set("pref_only", false);

    open_list =
        make_shared<standard_scalar_open_list::BestFirstOpenListFactory>
            (options)->create_state_open_list();
    timer.stop();
    timer.reset();
}

int FlawSearch::get_abstract_state_id(const State &state) const {
    int state_id = state.get_id().get_value();
    if (concrete_state_to_abstract_state.find(state_id)
        == concrete_state_to_abstract_state.end()) {
        concrete_state_to_abstract_state[state_id] =
            abstraction.get_abstract_state_id(state);
    }
    return concrete_state_to_abstract_state[state_id];
}

int FlawSearch::get_h_value(int abstract_state_id) const {
    return shortest_paths.get_goal_distance(abstract_state_id);
}

int FlawSearch::get_h_value(const State &state) const {
    return get_h_value(get_abstract_state_id(state));
}

bool FlawSearch::is_f_optimal_transition(int abstract_state_id,
                                         const Transition &tr) const {
    int source_h_value = get_h_value(abstract_state_id);
    int target_h_value = get_h_value(tr.target_id);
    int op_cost = task_proxy.get_operators()[tr.op_id].get_cost();
    return source_h_value - op_cost == target_h_value;
}

const vector<Transition> &FlawSearch::get_transitions(
    int abstract_state_id) const {
    return abstraction.get_transition_system().
           get_outgoing_transitions().at(abstract_state_id);
}

void FlawSearch::add_flaw(const State &state) {
    if (min_flaw_h > get_h_value(state)) {
        flawed_states.clear();
    }
    if (min_flaw_h >= get_h_value(state)) {
        min_flaw_h = get_h_value(state);
        if (flawed_states.count(get_abstract_state_id(state)) == 0) {
            flawed_states[get_abstract_state_id(state)] =
                utils::HashSet<State>();
        }
        flawed_states[get_abstract_state_id(state)].insert(state);
    }
}

void FlawSearch::initialize() {
    ++num_searches;
    g_bound = 0;
    f_bound = get_h_value(abstraction.get_initial_state().get_id());
    min_flaw_h = INF;
    open_list->clear();
    search_space = utils::make_unique_ptr<SearchSpace>(*state_registry);
    statistics = utils::make_unique_ptr<SearchStatistics>(utils::Verbosity::SILENT);

    concrete_state_to_abstract_state.clear();
    flawed_states.clear();

    State initial_state = state_registry->get_initial_state();
    EvaluationContext eval_context(initial_state, 0, false, statistics.get());

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
            /// Completely explored state space
            return FAILED;
        }
        StateID id = open_list->remove_min();
        State s = state_registry->lookup_state(id);
        node.emplace(search_space->get_node(s));

        if (node->is_closed()) {
            continue;
        }

        if (node->get_real_g() + get_h_value(s) > f_bound) {
            continue;
        }

        EvaluationContext eval_context(s, node->get_g(), false, statistics.get());
        node->close();
        assert(!node->is_dead_end());
        ++num_overall_expanded_concrete_states;
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

    // Transition and operators that are outgoing of the current concrete state
    // and stay in the same f-layer
    utils::HashSet<OperatorID> abstraction_ops;
    utils::HashSet<Transition> abstraction_trs;
    generate_abstraction_operators(s, abstraction_ops, abstraction_trs);

    // Transitions and operators which are (in)applicable
    utils::HashSet<Transition> valid_trs;
    utils::HashSet<Transition> invalid_trs;
    utils::HashSet<OperatorID> valid_ops;
    prune_operators(applicable_ops, abstraction_ops, abstraction_trs,
                    valid_ops, valid_trs, invalid_trs);

    if (debug && false) {
        utils::g_log << "STATE " << get_abstract_state_id(s) << ":" << endl;
        utils::g_log << "All trs: "
                     << vector<Transition>(abstraction_trs.begin(),
                              abstraction_trs.end()) << endl;
        utils::g_log << "Applicable trs: "
                     << vector<Transition>(valid_trs.begin(),
                              valid_trs.end()) << endl;
        utils::g_log << "Inapplicable trs: "
                     << vector<Transition>(invalid_trs.begin(),
                              invalid_trs.end()) << endl;
    }

    if (!invalid_trs.empty()) {
        add_flaw(s);
    }

    // Lookup table from op_ids to successor states in abstraction
    utils::HashMap<int, utils::HashSet<int>> abstract_successor_states;
    for (const Transition &tr : valid_trs) {
        if (abstract_successor_states.count(tr.op_id) == 0) {
            abstract_successor_states[tr.op_id] = utils::HashSet<int>();
        }
        abstract_successor_states[tr.op_id].insert(tr.target_id);
    }

    for (const OperatorID &op_id : valid_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        State succ_state = state_registry->get_successor_state(s, op);
        statistics->inc_generated();
        SearchNode succ_node = search_space->get_node(succ_state);

        // check for path deviation and if a valid tr exists
        int successor_ab_id = get_abstract_state_id(succ_state);
        bool valid_tr = false;
        for (int as_id : abstract_successor_states[op_id.get_index()]) {
            if (as_id != successor_ab_id) {
                add_flaw(s);
            } else {
                valid_tr = true;
            }
        }

        if (!valid_tr) {
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
    utils::HashSet<OperatorID> &abstraction_ops,
    utils::HashSet<Transition> &abstraction_trs) const {
    int abstract_state_id = get_abstract_state_id(state);

    for (const Transition &tr : get_transitions(abstract_state_id)) {
        if (is_f_optimal_transition(abstract_state_id, tr)) {
            abstraction_ops.insert(OperatorID(tr.op_id));
            abstraction_trs.insert(tr);
        }
    }
}

void FlawSearch::prune_operators(
    const vector<OperatorID> &applicable_ops,
    const utils::HashSet<OperatorID> &abstraction_ops,
    const utils::HashSet<Transition> &abstraction_trs,
    utils::HashSet<OperatorID> &valid_ops,
    utils::HashSet<Transition> &valid_trs,
    utils::HashSet<Transition> &invalid_trs) const {
    // Intersection of unordered maps
    // TODO: Maybe we should use std::set to use the std::intersection method
    for (const OperatorID &elem : applicable_ops) {
        if (abstraction_ops.count(elem)) {
            valid_ops.insert(elem);
        }
    }

    for (const Transition &tr : abstraction_trs) {
        if (valid_ops.count(OperatorID(tr.op_id)) == 0) {
            invalid_trs.insert(tr);
        } else {
            valid_trs.insert(tr);
        }
    }
}

unique_ptr<Flaw>
FlawSearch::create_flaw(const State &state, int abstract_state_id) {
    assert(abstraction.get_abstract_state_id(state) == abstract_state_id);
    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(state, applicable_ops);
    for (const Transition &tr : get_transitions(abstract_state_id)) {
        // same f-layer
        if (is_f_optimal_transition(abstract_state_id, tr)) {
            OperatorID op_id(tr.op_id);

            // Applicability flaw
            if (find(applicable_ops.begin(), applicable_ops.end(),
                     op_id) == applicable_ops.end()) {
                int h_value = get_h_value(abstract_state_id);
                return utils::make_unique_ptr<Flaw>(
                    move(State(state)),
                    get_cartesian_set(task_proxy.get_operators()
                                      [tr.op_id].get_preconditions()),
                    FlawReason::NOT_APPLICABLE, abstract_state_id, h_value);
            }
            OperatorProxy op = task_proxy.get_operators()[op_id];
            State succ_state = state_registry->get_successor_state(state, op);
            int successor_ab_id = get_abstract_state_id(succ_state);
            if (tr.target_id != successor_ab_id) {
                const AbstractState *deviated_abstact_state =
                    &abstraction.get_state(tr.target_id);
                int h_value = get_h_value(abstract_state_id);
                return utils::make_unique_ptr<Flaw>(
                    move(State(state)),
                    deviated_abstact_state->regress(
                        task_proxy.get_operators()[op_id]),
                    FlawReason::PATH_DEVIATION,
                    abstract_state_id,
                    h_value);
            }
        }
    }
    utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
    return nullptr;
}

CartesianSet FlawSearch::get_cartesian_set(const ConditionsProxy &conditions) const {
    CartesianSet cartesian_set(domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(condition.get_variable().get_id(),
                                       condition.get_value());
    }
    return cartesian_set;
}

SearchStatus
FlawSearch::search_for_flaws() {
    initialize();
    size_t cur_expanded_states = num_overall_expanded_concrete_states;
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        search_status = step();
    }

    if (debug) {
        cout << endl;
        utils::g_log << "Expanded "
                     << num_overall_expanded_concrete_states - cur_expanded_states
                     << " states!" << endl;
        utils::g_log << "Flawed States: " << endl;
        if (search_status == FAILED) {
            for (auto const &pair : flawed_states) {
                for (const State &s : pair.second) {
                    utils::g_log << "<" << pair.first << "," << s.get_id()
                                 << ">: " << *create_flaw(s, pair.first)
                                 << endl;
                }
            }
        }
    }
    return search_status;
}

unique_ptr<Flaw> FlawSearch::get_flaw(const pair<int, int> &new_state_ids) {
    if (debug && new_state_ids.first != -1) {
        utils::g_log << "New abstract states: <" << new_state_ids.first << ",h="
                     << get_h_value(new_state_ids.first)
                     << "> and <" << new_state_ids.second << ",h="
                     << get_h_value(new_state_ids.second) << ">"
                     << " old-h="
                     << min_flaw_h << endl;
    }
    assert(new_state_ids.first == -1
           || get_h_value(new_state_ids.first) >= min_flaw_h);
    assert(new_state_ids.second == -1
           || get_h_value(new_state_ids.second) >= min_flaw_h);

    // Fast reuse of flaws
    bool fast = true;

    timer.resume();
    if (fast && new_state_ids.first != -1 && new_state_ids.second != -1) {
        auto to_process = flawed_states.begin();
        flawed_states.erase(flawed_states.begin());
        for (const State &s : to_process->second) {
            if (task_properties::is_goal_state(task_proxy, s)) {
                return nullptr;
            }
            if (get_h_value(s) == min_flaw_h) {
                // TODO: we probably do not need to call the
                // refinement hierarchy
                add_flaw(s);
            }
        }
    }

    // New search if necessary
    auto search_status = SearchStatus::FAILED;
    if (!fast || flawed_states.empty()) {
        search_status = search_for_flaws();
    }

    // Flaws to refine are present
    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        ++num_overall_refined_flaws;
        auto flaw = create_flaw(
            *flawed_states.begin()->second.begin(),
            flawed_states.begin()->first);
        flawed_states.begin()->second.erase(flawed_states.begin()->second.begin());
        return flaw;
    }

    assert(search_status == SOLVED);
    return nullptr;
}

void FlawSearch::print_statistics() const {
    utils::g_log << endl;
    utils::g_log << "#Flaw searches: " << num_searches << endl;
    utils::g_log << "#Flaws refined: " << num_overall_refined_flaws << endl;
    utils::g_log << "#Expanded concrete states: "
                 << num_overall_expanded_concrete_states << endl;
    utils::g_log << "Flaw search time: " << timer << endl;
    utils::g_log << "Avg flaws refined: "
                 << num_overall_refined_flaws / (float)num_searches << endl;
    utils::g_log << "Avg expanded concrete states: "
                 << num_overall_expanded_concrete_states / (float)num_searches
                 << endl;
    utils::g_log << "Avg Flaw search time: "
                 << timer() / (float)num_searches << endl;
    utils::g_log << endl;
}
}
