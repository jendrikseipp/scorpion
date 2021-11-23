#include "flaw_search.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "transition_system.h"
#include "shortest_paths.h"
#include "split_selector.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../evaluators/g_evaluator.h"
#include "../open_lists/best_first_open_list.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <iterator>
#include <optional.hh>

using namespace std;

namespace cegar {
CartesianSet FlawSearch::get_cartesian_set(const ConditionsProxy &conditions) const {
    CartesianSet cartesian_set(domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(condition.get_variable().get_id(),
                                       condition.get_value());
    }
    return cartesian_set;
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
    if (pick_flaw == PickFlaw::MIN_H_SINGLE
        || pick_flaw == PickFlaw::MIN_H_BATCH) {
        if (best_flaw_h > get_h_value(state)) {
            flawed_states.clear();
        }
        if (best_flaw_h >= get_h_value(state)) {
            best_flaw_h = get_h_value(state);
            flawed_states[get_abstract_state_id(state)].insert(state);
        }
    } else if (pick_flaw == PickFlaw::MAX_H_SINGLE) {
        if (best_flaw_h < get_h_value(state)) {
            flawed_states.clear();
        }
        if (best_flaw_h <= get_h_value(state)) {
            best_flaw_h = get_h_value(state);
            flawed_states[get_abstract_state_id(state)].insert(state);
        }
    } else {
        // RANDOM SINGLE
        flawed_states[get_abstract_state_id(state)].insert(state);
    }
}

void FlawSearch::initialize() {
    ++num_searches;
    last_refined_abstract_state_id = -1;
    best_flaw_h = (pick_flaw == PickFlaw::MAX_H_SINGLE) ? -INF : INF;
    open_list->clear();
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
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
    if (open_list->empty()) {
        /// Completely explored state space
        return FAILED;
    }
    StateID id = open_list->remove_min();
    State s = state_registry->lookup_state(id);
    SearchNode node = search_space->get_node(s);

    assert(!node.is_closed());
    assert(node.get_real_g() + get_h_value(s)
           <= get_h_value(abstraction.get_initial_state().get_id()));

    node.close();
    assert(!node.is_dead_end());
    ++num_overall_expanded_concrete_states;
    statistics->inc_expanded();

    if (task_properties::is_goal_state(task_proxy, s)) {
        Plan plan;
        search_space->trace_path(s, plan);
        //PlanManager plan_mgr;
        //cout << "FOUND CONCRETE SOLUTION: " << endl;
        //plan_mgr.save_plan(plan, task_proxy, true);
        return SOLVED;
    }

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(s, applicable_ops);

    // Check for each tr if the op is applicable or if there is a deviation
    for (const Transition &tr : get_transitions(get_abstract_state_id(s))) {
        // same f-layer
        if (is_f_optimal_transition(get_abstract_state_id(s), tr)) {
            OperatorID op_id(tr.op_id);

            // Applicability flaw
            if (find(applicable_ops.begin(), applicable_ops.end(),
                     op_id) == applicable_ops.end()) {
                add_flaw(s);
                continue;
            }
            OperatorProxy op = task_proxy.get_operators()[op_id];
            State succ_state = state_registry->get_successor_state(s, op);
            int successor_ab_id = get_abstract_state_id(succ_state);
            // Deviation flaw
            if (tr.target_id != successor_ab_id) {
                add_flaw(s);
                continue;
            }
            // No flaw found
            statistics->inc_generated();
            SearchNode succ_node = search_space->get_node(succ_state);

            assert(!succ_node.is_dead_end());

            if (succ_node.is_new()) {
                // We have not seen this state before.
                // Evaluate and create a new node.

                // Careful: succ_node.get_g() is not available here yet,
                // hence the stupid computation of succ_g.
                // TODO: Make this less fragile.
                int succ_g = node.get_g() + op.get_cost();

                EvaluationContext succ_eval_context(
                    succ_state, succ_g, false, statistics.get());
                statistics->inc_evaluated_states();

                succ_node.open(node, op, op.get_cost());

                open_list->insert(succ_eval_context, succ_state.get_id());
            }
        }
    }
    return IN_PROGRESS;
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
                const AbstractState &abstract_state =
                    abstraction.get_state(abstract_state_id);
                return split_selector.pick_split(
                    abstract_state, state,
                    get_cartesian_set(
                        task_proxy.get_operators()[tr.op_id].get_preconditions()),
                    rng);
            }
            OperatorProxy op = task_proxy.get_operators()[op_id];
            State succ_state = state_registry->get_successor_state(state, op);

            if (tr.target_id != get_abstract_state_id(succ_state)) {
                const AbstractState &abstract_state =
                    abstraction.get_state(abstract_state_id);
                const AbstractState *deviated_abstact_state =
                    &abstraction.get_state(tr.target_id);
                return split_selector.pick_split(
                    abstract_state, state,
                    deviated_abstact_state->regress(op), rng);
            }
            assert(pick_flaw == PickFlaw::MAX_H_SINGLE
                   || pick_flaw == PickFlaw::RANDOM_H_SINGLE);
        }
    }
    utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
    return nullptr;
}

// Quite similar to create_flaw. Maybe we want to refactor it at some point
unique_ptr<Flaw> FlawSearch::create_max_cover_flaw(
    const utils::HashSet<State> &states, int abstract_state_id) {
    assert(pick_flaw == PickFlaw::MIN_H_BATCH);

    vector<State> flawed_states;
    vector<CartesianSet> flawed_cartesian_sets;

    for (const State &state : states) {
        vector<OperatorID> applicable_ops;
        successor_generator.generate_applicable_ops(state,
                                                    applicable_ops);
        for (const Transition &tr : get_transitions(abstract_state_id)) {
            assert(abstraction.get_abstract_state_id(state) == abstract_state_id);
            // same f-layer
            if (is_f_optimal_transition(abstract_state_id, tr)) {
                OperatorID op_id(tr.op_id);

                // Applicability flaw
                if (find(applicable_ops.begin(), applicable_ops.end(),
                         op_id) == applicable_ops.end()) {
                    flawed_states.push_back(state);
                    flawed_cartesian_sets.push_back(
                        get_cartesian_set(
                            task_proxy.get_operators()[tr.op_id].get_preconditions()));
                } else {
                    // Deviation Flaw
                    OperatorProxy op = task_proxy.get_operators()[op_id];
                    assert(tr.target_id != get_abstract_state_id(
                               state_registry->get_successor_state(state, op)));

                    const AbstractState *deviated_abstact_state =
                        &abstraction.get_state(tr.target_id);

                    flawed_states.push_back(state);
                    flawed_cartesian_sets.push_back(
                        deviated_abstact_state->regress(op));
                }
            }
        }
    }

    return split_selector.pick_split(
        abstraction.get_state(abstract_state_id),
        flawed_states, flawed_cartesian_sets);
}

SearchStatus FlawSearch::search_for_flaws() {
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

unique_ptr<Flaw> FlawSearch::get_random_single_flaw() {
    auto search_status = search_for_flaws();

    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        ++num_overall_refined_flaws;

        int rng_abstract_state_id =
            next(flawed_states.begin(), rng(flawed_states.size()))->first;
        auto rng_state =
            *next(flawed_states.at(rng_abstract_state_id).begin(),
                  rng(flawed_states.at(rng_abstract_state_id).size()));

        auto flaw = create_flaw(rng_state, rng_abstract_state_id);
        best_flaw_h = get_h_value(flaw->abstract_state_id);
        return flaw;
    }
    assert(search_status == SOLVED);
    return nullptr;
}

unique_ptr<Flaw> FlawSearch::get_single_flaw() {
    auto search_status = search_for_flaws();

    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        ++num_overall_refined_flaws;
        auto flaw = create_flaw(
            *flawed_states.begin()->second.begin(),
            flawed_states.begin()->first);
        return flaw;
    }
    assert(search_status == SOLVED);
    return nullptr;
}

unique_ptr<Flaw>
FlawSearch::get_min_h_batch_flaw(const pair<int, int> &new_state_ids) {
    // Handle flaws of refined abstract state
    if (new_state_ids.first != -1) {
        utils::HashSet<State> states_to_handle =
            flawed_states.at(last_refined_abstract_state_id);
        flawed_states.erase(last_refined_abstract_state_id);
        for (const State &s : states_to_handle) {
            if (task_properties::is_goal_state(task_proxy, s)) {
                return nullptr;
            }
            if (get_h_value(s) == best_flaw_h) {
                // TODO: we probably do not need to call the
                // refinement hierarchy
                add_flaw(s);
            }
        }
    }

    auto search_status = SearchStatus::FAILED;
    if (flawed_states.empty()) {
        search_status = search_for_flaws();
    }

    // Flaws to refine are present
    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        ++num_overall_refined_flaws;
        unique_ptr<Flaw> flaw;

        int abstract_state_id = flawed_states.begin()->first;
        const State &state = *flawed_states[abstract_state_id].begin();

        if (split_selector.get_pick_split_strategy() == PickSplit::MAX_COVER) {
            flaw = create_max_cover_flaw(flawed_states.begin()->second,
                                         abstract_state_id);
        } else {
            flaw = create_flaw(state, abstract_state_id);
        }

        // Remove flawed concrete state from list
        flawed_states[abstract_state_id].erase(state);
        return flaw;
    }

    assert(search_status == SOLVED);
    return nullptr;
}

FlawSearch::FlawSearch(const shared_ptr<AbstractTask> &task,
                       const vector<int> &domain_sizes,
                       const Abstraction &abstraction,
                       const ShortestPaths &shortest_paths,
                       utils::RandomNumberGenerator &rng,
                       PickFlaw pick_flaw,
                       PickSplit pick_split,
                       bool debug) :
    task_proxy(*task),
    domain_sizes(domain_sizes),
    abstraction(abstraction),
    shortest_paths(shortest_paths),
    split_selector(task, pick_split),
    rng(rng),
    pick_flaw(pick_flaw),
    debug(debug),
    open_list(nullptr),
    state_registry(utils::make_unique_ptr<StateRegistry>(task_proxy)),
    search_space(nullptr),
    statistics(nullptr),
    successor_generator(
        successor_generator::g_successor_generators[task_proxy]),
    last_refined_abstract_state_id(-1),
    best_flaw_h((pick_flaw == PickFlaw::MAX_H_SINGLE) ? -INF : INF),
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

unique_ptr<Flaw> FlawSearch::get_flaw(const pair<int, int> &new_state_ids) {
    if (debug && new_state_ids.first != -1) {
        utils::g_log << "New abstract states: <" << new_state_ids.first << ",h="
                     << get_h_value(new_state_ids.first)
                     << "> and <" << new_state_ids.second << ",h="
                     << get_h_value(new_state_ids.second) << ">"
                     << " old-h="
                     << best_flaw_h << endl;
    }
    assert(new_state_ids.first == -1
           || get_h_value(new_state_ids.first) >= best_flaw_h);
    assert(new_state_ids.second == -1
           || get_h_value(new_state_ids.second) >= best_flaw_h);

    timer.resume();
    unique_ptr<Flaw> flaw = nullptr;

    switch (pick_flaw) {
    case PickFlaw::RANDOM_H_SINGLE:
        flaw = get_random_single_flaw();
        break;
    case PickFlaw::MIN_H_SINGLE:
        flaw = get_single_flaw();
        break;
    case PickFlaw::MAX_H_SINGLE:
        flaw = get_single_flaw();
        break;
    case PickFlaw::MIN_H_BATCH:
        flaw = get_min_h_batch_flaw(new_state_ids);
        break;
    default:
        utils::g_log << "Invalid pick flaw strategy: " << static_cast<int>(pick_flaw)
                     << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }

    if (flaw != nullptr) {
        last_refined_abstract_state_id = flaw->abstract_state_id;
        concrete_state_to_abstract_state.clear();
        // utils::g_log << "Selected flaw: " << *flaw << endl;
    }
    timer.stop();
    return flaw;
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
